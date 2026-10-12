// SPDX-License-Identifier: GPL-3.0-only
//! The board's USB serial link. The app owns the port; the settings window and the developer API
//! (devapi.rs) both talk through it.
//!
//! Lines from the board go to the window as `board-line` events and to every developer API subscriber.
//! The port is found by USB ID (Espressif USB Serial/JTAG), confirmed with `hello`, and reopened after an unplug.
//! One thread reads, blocking until the board sends something; another writes queued lines through a clone of
//! the port as soon as they are queued.

use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::{ErrorKind, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::Serialize;
use serialport::{SerialPort, SerialPortType};
use tauri::{AppHandle, Emitter};

/// ESP32-S3 USB Serial/JTAG controller. Every ESP32-S3/C3 board has this ID, so a port counts as the board only
/// once it answers `hello` as blue-baton
const USB_VID: u16 = 0x303A;
const USB_PID: u16 = 0x1001;
/// The app's own hello (the UI's ids start at 1 or more). Its reply is not passed on
const HELLO: &str = r#"{"id":0,"cmd":"hello"}"#;
/// How long to wait for the hello reply. It is asked again every PROBE_REPEAT in case the board is still booting
/// (just plugged in or flashed)
const PROBE_FOR: Duration = Duration::from_secs(2);
const PROBE_REPEAT: Duration = Duration::from_millis(500);
/// A port that does not answer is left alone this long (or until it goes away), so another ESP32 board is neither
/// held nor asked again and again
const NOT_OURS_FOR: Duration = Duration::from_secs(60);
/// Longest a read waits while the board is quiet. Only `release` waits for it; writes have their own thread
const READ_TIMEOUT: Duration = Duration::from_secs(1);
/// board.log moves to board.log.1 at this size
const LOG_MAX: u64 = 1024 * 1024;

#[derive(Clone, Serialize)]
pub struct Status {
    /// searching | busy | open | released | other (an ESP32-S3 port that does not answer as Blue Baton: a board
    /// without our firmware, which the settings window offers to install)
    pub state: &'static str,
    pub port: Option<String>,
    pub released: bool,
    pub clients: usize,
}

pub struct Board {
    app: AppHandle,
    out: Mutex<Option<Sender<String>>>,
    status: Mutex<Status>,
    status_changed: Condvar,
    released: AtomicBool,
    subscribers: Mutex<Vec<Sender<String>>>,
    log: Mutex<Option<Log>>,
}

enum Found {
    Board(String, LineReader),
    /// Another program has this port open (a serial monitor, the flasher, or tools/bridge.py)
    Busy(String),
    Nothing,
}

fn now() -> f64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0)
}

impl Board {
    pub fn start(app: AppHandle) -> Arc<Self> {
        let board = Arc::new(Board {
            app,
            out: Mutex::new(None),
            status: Mutex::new(Status { state: "searching", port: None, released: false, clients: 0 }),
            status_changed: Condvar::new(),
            released: AtomicBool::new(false),
            subscribers: Mutex::new(Vec::new()),
            log: Mutex::new(None),
        });
        let worker = board.clone();
        thread::Builder::new()
            .name("board-serial".into())
            .spawn(move || worker.run())
            .expect("spawn serial thread");
        board
    }

    pub fn status(&self) -> Status {
        let mut s = self.status.lock().unwrap().clone();
        s.clients = self.subscribers.lock().unwrap().len();
        s
    }

    /// Send one line (one JSON command). Err if the port is not open
    pub fn send(&self, line: &str) -> Result<(), &'static str> {
        let line = line.trim();
        if line.is_empty() || line.contains('\n') {
            return Err("one line required");
        }
        let out = self.out.lock().unwrap();
        let tx = out.as_ref().ok_or("port not open")?;
        tx.send(line.to_string()).map_err(|_| "port not open")?;
        self.write_log(&format!(">> {line}"));
        Ok(())
    }

    /// Close the port and stop reopening it (for flashing). Returns once the port is closed, so a flasher can
    /// open it right away. `acquire` resumes
    pub fn release(&self) {
        self.released.store(true, Ordering::SeqCst);
        let status = self.status.lock().unwrap();
        // Longer than any wait in `run`
        let _ = self.status_changed.wait_timeout_while(status, Duration::from_secs(3), |s| s.state != "released");
    }

    pub fn acquire(&self) {
        self.released.store(false, Ordering::SeqCst);
    }

    pub fn subscribe(&self) -> Receiver<String> {
        let (tx, rx) = channel();
        self.subscribers.lock().unwrap().push(tx);
        rx
    }

    /// End every subscription (the developer API was turned off)
    pub fn unsubscribe_all(&self) {
        self.subscribers.lock().unwrap().clear();
    }

    /// Keep a copy of the lines in a file (developer API only), or stop with None. Pairing codes and PINs are
    /// masked (`redact`); what was typed is left out (`is_input_event`)
    pub fn set_log(&self, path: Option<&Path>) {
        *self.log.lock().unwrap() = path.and_then(Log::open);
    }

    fn write_log(&self, text: &str) {
        if is_input_event(text) {
            return;
        }
        if let Some(log) = self.log.lock().unwrap().as_mut() {
            log.write(&format!("{:.3} {}\n", now(), redact(text)));
        }
    }

    fn set_state(&self, state: &'static str, port: Option<String>) {
        let changed = {
            let mut s = self.status.lock().unwrap();
            let changed = s.state != state || s.port != port;
            s.state = state;
            s.port = port;
            s.released = self.released.load(Ordering::SeqCst);
            changed
        };
        self.status_changed.notify_all();
        if changed {
            let _ = self.app.emit("board-state", self.status());
        }
    }

    /// One line from the board (or a "#bridge ..." notice, as tools/bridge.py sends them)
    fn dispatch(&self, line: String) {
        let _ = self.app.emit("board-line", line.clone());
        self.subscribers.lock().unwrap().retain(|tx| tx.send(line.clone()).is_ok());
        self.write_log(&line);
    }

    /// Ports with the board's USB ID. On macOS each device has a cu.* and a tty.* node; cu.* is the one to open
    fn find_ports() -> Vec<String> {
        let mut found: Vec<String> = serialport::available_ports()
            .unwrap_or_default()
            .into_iter()
            .filter(|p| matches!(&p.port_type, SerialPortType::UsbPort(u) if u.vid == USB_VID && u.pid == USB_PID))
            .map(|p| p.port_name)
            .collect();
        if found.iter().any(|n| n.starts_with("/dev/cu.")) {
            found.retain(|n| n.starts_with("/dev/cu."));
        }
        found
    }

    /// Open the first port with the board's USB ID that answers hello as blue-baton. Ports that do not are noted
    /// in `not_ours` and skipped until NOT_OURS_FOR has passed or they go away
    fn find_board(&self, not_ours: &mut HashMap<String, Instant>) -> Found {
        let ports = Self::find_ports();
        not_ours.retain(|name, since| ports.contains(name) && since.elapsed() < NOT_OURS_FOR);
        let mut busy = None;
        for name in ports {
            if self.released.load(Ordering::SeqCst) {
                break;
            }
            if not_ours.contains_key(&name) {
                continue;
            }
            match serialport::new(&name, 115_200).timeout(PROBE_REPEAT).open() {
                Err(_) => busy = Some(name),
                Ok(port) => {
                    let mut reader = LineReader { port, pending: Vec::new() };
                    if self.probe(&mut reader) {
                        return Found::Board(name, reader);
                    }
                    not_ours.insert(name, Instant::now());
                }
            }
        }
        busy.map_or(Found::Nothing, Found::Busy)
    }

    /// Ask `hello` until a blue-baton reply comes or PROBE_FOR passes
    fn probe(&self, reader: &mut LineReader) -> bool {
        let start = Instant::now();
        let mut asked = start;
        if write_line(reader.port.as_mut(), HELLO).is_err() {
            return false;
        }
        while start.elapsed() < PROBE_FOR && !self.released.load(Ordering::SeqCst) {
            match reader.read_lines() {
                Ok(lines) if lines.iter().any(|l| is_hello_reply(l)) => return true,
                Ok(_) => {}
                Err(_) => return false,
            }
            if asked.elapsed() >= PROBE_REPEAT {
                if write_line(reader.port.as_mut(), HELLO).is_err() {
                    return false;
                }
                asked = Instant::now();
            }
        }
        false
    }

    fn run(self: Arc<Self>) {
        // Ports with the board's USB ID that did not answer hello as blue-baton, and since when
        let mut not_ours = HashMap::new();
        loop {
            if self.released.load(Ordering::SeqCst) {
                not_ours.clear(); // a board flashed meanwhile may be ours now
                self.set_state("released", None);
                thread::sleep(Duration::from_millis(200));
                continue;
            }
            let (name, mut reader) = match self.find_board(&mut not_ours) {
                Found::Board(name, reader) => (name, reader),
                Found::Busy(name) => {
                    self.set_state("busy", Some(name));
                    thread::sleep(Duration::from_secs(1));
                    continue;
                }
                Found::Nothing => {
                    match not_ours.keys().next().cloned() {
                        Some(other) => self.set_state("other", Some(other)),
                        None => self.set_state("searching", None),
                    }
                    thread::sleep(Duration::from_secs(1));
                    continue;
                }
            };
            let _ = reader.port.set_timeout(READ_TIMEOUT); // before the clone: on Windows they share it
            let (tx, rx) = channel::<String>();
            let Ok(writing) = start_writer(reader.port.as_ref(), rx) else {
                thread::sleep(Duration::from_secs(1));
                continue;
            };
            *self.out.lock().unwrap() = Some(tx);
            self.set_state("open", Some(name.clone()));
            self.dispatch(format!("#bridge open {name}"));
            self.pump(reader);
            *self.out.lock().unwrap() = None; // ends the writer
            let _ = writing.join();
            let released = self.released.load(Ordering::SeqCst);
            self.dispatch(if released { "#bridge closed (released)".into() } else { "#bridge closed (lost)".into() });
            self.set_state(if released { "released" } else { "searching" }, None);
            thread::sleep(Duration::from_millis(300));
        }
    }

    /// Pass lines on until the port fails (unplug) or is released. The port closes when this returns
    fn pump(&self, mut reader: LineReader) {
        while !self.released.load(Ordering::SeqCst) {
            match reader.read_lines() {
                Ok(lines) => lines.into_iter().for_each(|line| self.dispatch(line)),
                Err(_) => return,
            }
        }
    }
}

/// The read side of an open port: bytes in, lines out
struct LineReader {
    port: Box<dyn SerialPort>,
    pending: Vec<u8>,
}

impl LineReader {
    /// Wait up to the port's timeout for data and return the lines it completes (none if nothing came).
    /// Err once the port is gone (unplugged)
    fn read_lines(&mut self) -> std::io::Result<Vec<String>> {
        let mut buf = [0u8; 1024];
        match self.port.read(&mut buf) {
            // A raw-mode read waits for at least one byte, so 0 means the device hung up
            Ok(0) => return Err(ErrorKind::UnexpectedEof.into()),
            Ok(n) => self.pending.extend_from_slice(&buf[..n]),
            Err(e) if matches!(e.kind(), ErrorKind::TimedOut | ErrorKind::Interrupted) => {}
            Err(e) => return Err(e),
        }
        Ok(take_lines(&mut self.pending))
    }
}

/// The complete lines at the front of `pending`, without line ends; blank lines are dropped
fn take_lines(pending: &mut Vec<u8>) -> Vec<String> {
    let mut lines = Vec::new();
    while let Some(i) = pending.iter().position(|&c| c == b'\n') {
        let raw: Vec<u8> = pending.drain(..=i).collect();
        let line = String::from_utf8_lossy(&raw).trim_end_matches(['\r', '\n']).to_string();
        if !line.is_empty() {
            lines.push(line);
        }
    }
    if pending.len() > 8192 {
        pending.clear(); // no newline for a long time: not our protocol
    }
    lines
}

/// Write each queued line through a clone of `port` as soon as it comes, until the sender goes
fn start_writer(port: &dyn SerialPort, rx: Receiver<String>) -> std::io::Result<thread::JoinHandle<()>> {
    let mut out = port.try_clone()?;
    thread::Builder::new().name("board-write".into()).spawn(move || {
        for line in rx {
            // A board that does not read times this line out; the next one tries again
            let _ = write_line(out.as_mut(), &line);
        }
    })
}

/// No flush: the OS sends it right away, while flush (tcdrain) would wait, with no timeout, for as long as the
/// board does not read
fn write_line(port: &mut dyn SerialPort, line: &str) -> std::io::Result<()> {
    port.write_all(format!("{line}\n").as_bytes())
}

/// `KM< {"id":0,"ok":true,"fw":"blue-baton",...}`
fn is_hello_reply(line: &str) -> bool {
    line.find("KM< ")
        .and_then(|i| serde_json::from_str::<serde_json::Value>(&line[i + 4..]).ok())
        .is_some_and(|v| v["fw"] == "blue-baton")
}

/// `report` and `route` events carry what was typed (sent only while the UI's input monitor is on)
fn is_input_event(line: &str) -> bool {
    line.contains(r#""event":"report""#) || line.contains(r#""event":"route""#)
}

/// Mask all-digit `code` and `pin` values (passkeys typed or shown, numeric comparison codes, PINs).
/// Failure codes such as `"code":"hosts_full"` stay readable
fn redact(text: &str) -> String {
    let mut s = text.to_string();
    for key in ["\"code\":\"", "\"pin\":\""] {
        let mut from = 0;
        while let Some(i) = s[from..].find(key) {
            let start = from + i + key.len();
            let Some(len) = s[start..].find('"') else { break };
            if len > 0 && s[start..start + len].bytes().all(|b| b.is_ascii_digit()) {
                s.replace_range(start..start + len, "******");
                from = start + 6;
            } else {
                from = start + len;
            }
        }
    }
    s
}

/// The developer API's copy of the board lines. At LOG_MAX it moves to `<name>.1`, replacing the previous one
struct Log {
    path: PathBuf,
    file: File,
    len: u64,
}

impl Log {
    fn open(path: &Path) -> Option<Log> {
        if let Some(dir) = path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let file = OpenOptions::new().create(true).append(true).open(path).ok()?;
        let len = file.metadata().map(|m| m.len()).unwrap_or(0);
        Some(Log { path: path.to_path_buf(), file, len })
    }

    fn write(&mut self, text: &str) {
        if self.len + text.len() as u64 > LOG_MAX {
            let mut old = self.path.clone().into_os_string();
            old.push(".1");
            let _ = std::fs::rename(&self.path, old);
            // A new file, or the same one emptied if it could not be moved
            if let Ok(file) = File::create(&self.path) {
                self.file = file;
            }
            self.len = 0;
        }
        if self.file.write_all(text.as_bytes()).is_ok() {
            self.len += text.len() as u64;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn masks_codes_but_not_error_names() {
        assert_eq!(
            redact(r#">> {"id":1,"cmd":"host_passkey","code":"012345"}"#),
            r#">> {"id":1,"cmd":"host_passkey","code":"******"}"#
        );
        assert_eq!(redact(r#"KM< {"event":"numcmp","code":"482910"}"#), r#"KM< {"event":"numcmp","code":"******"}"#);
        assert_eq!(redact(r#">> {"cmd":"pin_set","pin":"1234"}"#), r#">> {"cmd":"pin_set","pin":"******"}"#);
        let err = r#"KM< {"id":2,"ok":false,"code":"hosts_full"}"#;
        assert_eq!(redact(err), err);
        assert_eq!(redact(r#"{"code":""}"#), r#"{"code":""}"#);
    }

    #[test]
    fn hello_reply_only_from_blue_baton() {
        assert!(is_hello_reply(
            r#"KM< {"id":0,"ok":true,"fw":"blue-baton","version":"0.7","combo_set":false,"wifi_feature":false}"#
        ));
        assert!(!is_hello_reply(r#"KM< {"id":0,"ok":true,"fw":"other"}"#));
        assert!(!is_hello_reply(r#"KM< {"id":0,"ok":false,"code":"bad_request","error":"cmd is missing"}"#));
        assert!(!is_hello_reply(r#"{"id":0,"ok":true,"fw":"blue-baton"}"#)); // no KM< prefix
        assert!(!is_hello_reply("I (312) km: \"fw\":\"blue-baton\""));
        assert!(!is_hello_reply("KM< {\"fw\":\"blue-baton\""));
    }

    #[test]
    fn input_events_stay_out_of_the_log() {
        assert!(is_input_event(r#"KM< {"event":"report","addr":"x","handle":20,"data":"0000040000000000"}"#));
        assert!(is_input_event(r#"KM< {"event":"route","host":1,"keys":[4]}"#));
        assert!(!is_input_event(r#"KM< {"event":"report_map","addr":"x","svc":1,"len":50}"#));
        assert!(!is_input_event(r#"KM< {"event":"report_ref","addr":"x","val":1,"id":1,"type":1,"svc":1}"#));
        assert!(!is_input_event(r#"KM< {"event":"activity","addr":"x"}"#));
    }

    #[test]
    fn splits_lines_and_keeps_the_rest() {
        let mut pending = b"KM< {\"a\":1}\r\n\nI (1) km: x\nKM< {\"b\"".to_vec();
        assert_eq!(take_lines(&mut pending), ["KM< {\"a\":1}", "I (1) km: x"]);
        assert_eq!(pending, b"KM< {\"b\"");
        pending.extend_from_slice(b":2}\n");
        assert_eq!(take_lines(&mut pending), ["KM< {\"b\":2}"]);
        assert!(pending.is_empty());
        let mut junk = vec![b'x'; 9000];
        assert!(take_lines(&mut junk).is_empty());
        assert!(junk.is_empty());
    }

    #[test]
    fn log_rotates_at_the_cap() {
        let dir = std::env::temp_dir().join(format!("bluebaton-log-test-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let path = dir.join("board.log");
        let old = dir.join("board.log.1");
        let line = format!("{}\n", "x".repeat(999));
        let mut log = Log::open(&path).unwrap();
        for _ in 0..(LOG_MAX / 1000) {
            log.write(&line);
        }
        assert!(!old.exists());
        log.write(&line);
        log.write(&line);
        assert!(std::fs::metadata(old).unwrap().len() <= LOG_MAX);
        assert_eq!(std::fs::metadata(&path).unwrap().len(), 2000);
        drop(log);
        // Reopening continues from the current size
        let log = Log::open(&path).unwrap();
        assert_eq!(log.len, 2000);
        let _ = std::fs::remove_dir_all(dir);
    }
}
