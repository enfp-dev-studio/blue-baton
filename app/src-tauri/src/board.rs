//! The board's USB serial link. The app owns the port; the settings window and the developer API
//! (devapi.rs) both talk through it.
//!
//! Lines from the board go to the window as `board-line` events and to every developer API subscriber.
//! The port is found by USB ID (Espressif USB Serial/JTAG) and reopened after an unplug.

use std::fs::{File, OpenOptions};
use std::io::{ErrorKind, Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::Serialize;
use serialport::{SerialPort, SerialPortType};
use tauri::{AppHandle, Emitter};

/// ESP32-S3 USB Serial/JTAG controller
const USB_VID: u16 = 0x303A;
const USB_PID: u16 = 0x1001;

#[derive(Clone, Serialize)]
pub struct Status {
    /// searching | busy | open | released
    pub state: &'static str,
    pub port: Option<String>,
    pub released: bool,
    pub clients: usize,
}

pub struct Board {
    app: AppHandle,
    out: Mutex<Option<Sender<String>>>,
    status: Mutex<Status>,
    released: AtomicBool,
    subscribers: Mutex<Vec<Sender<String>>>,
    log: Mutex<Option<File>>,
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

    /// Close the port and stop reopening it (for flashing). `acquire` resumes
    pub fn release(&self) {
        self.released.store(true, Ordering::SeqCst);
    }

    pub fn acquire(&self) {
        self.released.store(false, Ordering::SeqCst);
    }

    pub fn subscribe(&self) -> Receiver<String> {
        let (tx, rx) = channel();
        self.subscribers.lock().unwrap().push(tx);
        rx
    }

    /// Keep a copy of every line in a file (developer API only). Pairing codes and PINs are masked (`redact`)
    pub fn set_log(&self, file: Option<File>) {
        *self.log.lock().unwrap() = file;
    }

    pub fn open_log(path: &std::path::Path) -> Option<File> {
        if let Some(dir) = path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        OpenOptions::new().create(true).append(true).open(path).ok()
    }

    fn write_log(&self, text: &str) {
        if let Some(f) = self.log.lock().unwrap().as_mut() {
            let _ = writeln!(f, "{:.3} {}", now(), redact(text));
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

    /// The board's port. On macOS each device has a cu.* and a tty.* node; cu.* is the one to open
    fn find_port() -> Option<String> {
        let ports = serialport::available_ports().ok()?;
        let mut found: Vec<String> = ports
            .into_iter()
            .filter(|p| matches!(&p.port_type, SerialPortType::UsbPort(u) if u.vid == USB_VID && u.pid == USB_PID))
            .map(|p| p.port_name)
            .collect();
        found.sort_by_key(|n| !n.starts_with("/dev/cu."));
        found.into_iter().next()
    }

    fn run(self: Arc<Self>) {
        loop {
            if self.released.load(Ordering::SeqCst) {
                self.set_state("released", None);
                thread::sleep(Duration::from_millis(200));
                continue;
            }
            let Some(name) = Self::find_port() else {
                self.set_state("searching", None);
                thread::sleep(Duration::from_secs(1));
                continue;
            };
            let port = match serialport::new(&name, 115_200).timeout(Duration::from_millis(20)).open() {
                Ok(p) => p,
                Err(_) => {
                    // Another program has it (a serial monitor, the flasher, or tools/bridge.py)
                    self.set_state("busy", Some(name));
                    thread::sleep(Duration::from_secs(1));
                    continue;
                }
            };
            let (tx, rx) = channel::<String>();
            *self.out.lock().unwrap() = Some(tx);
            self.set_state("open", Some(name.clone()));
            self.dispatch(format!("#bridge open {name}"));
            self.pump(port, rx);
            *self.out.lock().unwrap() = None;
            let released = self.released.load(Ordering::SeqCst);
            self.dispatch(if released { "#bridge closed (released)".into() } else { "#bridge closed (lost)".into() });
            self.set_state(if released { "released" } else { "searching" }, None);
            thread::sleep(Duration::from_millis(300));
        }
    }

    /// Read lines and write queued commands until the port fails (unplug) or is released
    fn pump(&self, mut port: Box<dyn SerialPort>, rx: Receiver<String>) {
        let mut buf = [0u8; 1024];
        let mut pending: Vec<u8> = Vec::new();
        loop {
            if self.released.load(Ordering::SeqCst) {
                return;
            }
            while let Ok(line) = rx.try_recv() {
                let ok = port.write_all(line.as_bytes()).and_then(|_| port.write_all(b"\n")).and_then(|_| port.flush());
                if ok.is_err() {
                    return;
                }
            }
            match port.read(&mut buf) {
                Ok(0) => {}
                Ok(n) => {
                    pending.extend_from_slice(&buf[..n]);
                    while let Some(i) = pending.iter().position(|&c| c == b'\n') {
                        let raw: Vec<u8> = pending.drain(..=i).collect();
                        let line = String::from_utf8_lossy(&raw).trim_end_matches(['\r', '\n']).to_string();
                        if !line.is_empty() {
                            self.dispatch(line);
                        }
                    }
                    if pending.len() > 8192 {
                        pending.clear(); // no newline for a long time: not our protocol
                    }
                }
                Err(e) if e.kind() == ErrorKind::TimedOut || e.kind() == ErrorKind::Interrupted => {}
                Err(_) => return,
            }
        }
    }
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

#[cfg(test)]
mod tests {
    use super::redact;

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
}
