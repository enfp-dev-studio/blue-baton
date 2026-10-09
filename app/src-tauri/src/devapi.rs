//! Developer API on 127.0.0.1:8766, the same HTTP interface as firmware/tools/bridge.py, so the flash
//! helper (/release, /acquire) and monitoring tools keep working while the app owns the port.
//! Off by default (tray menu → Developer API), because board lines include typed passkeys.
//!
//!   GET  /events   board lines as server-sent events
//!   POST /send     one JSON command line
//!   POST /release  close the port for flashing; POST /acquire reopens it
//!   GET  /status   {"port", "open", "released", "clients"}
//!   GET  other     the settings UI (bundled assets), "/" → "/?bridge"
//!
//! Security: bound to 127.0.0.1 only. POST is accepted only without an Origin header (command line tools)
//! or from this server's own origin. No CORS headers, so other sites cannot read /events.

use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::RecvTimeoutError;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use tauri::{AppHandle, Manager};

use crate::board::Board;

pub const ADDR: &str = "127.0.0.1:8766";
const ORIGINS: [&str; 2] = ["http://127.0.0.1:8766", "http://localhost:8766"];

pub struct DevApi {
    stop: Arc<AtomicBool>,
}

impl DevApi {
    pub fn start(app: AppHandle, board: Arc<Board>) -> std::io::Result<Self> {
        let listener = TcpListener::bind(ADDR)?;
        listener.set_nonblocking(true)?;
        let stop = Arc::new(AtomicBool::new(false));
        let flag = stop.clone();
        thread::Builder::new().name("devapi".into()).spawn(move || {
            while !flag.load(Ordering::SeqCst) {
                match listener.accept() {
                    Ok((stream, _)) => {
                        let (app, board) = (app.clone(), board.clone());
                        thread::spawn(move || {
                            let _ = handle(stream, &app, &board);
                        });
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(50)),
                    Err(_) => thread::sleep(Duration::from_millis(200)),
                }
            }
        })?;
        Ok(DevApi { stop })
    }

    pub fn stop(&self) {
        self.stop.store(true, Ordering::SeqCst);
    }
}

struct Request {
    method: String,
    path: String,
    origin: Option<String>,
    body: String,
}

fn read_request(stream: &TcpStream) -> std::io::Result<Request> {
    stream.set_read_timeout(Some(Duration::from_secs(5)))?;
    let mut reader = BufReader::new(stream);
    let mut first = String::new();
    reader.read_line(&mut first)?;
    let mut parts = first.split_whitespace();
    let method = parts.next().unwrap_or("").to_string();
    let path = parts.next().unwrap_or("/").to_string();
    let (mut length, mut origin) = (0usize, None);
    loop {
        let mut line = String::new();
        if reader.read_line(&mut line)? == 0 || line == "\r\n" || line == "\n" {
            break;
        }
        if let Some((k, v)) = line.split_once(':') {
            let v = v.trim().to_string();
            match k.trim().to_ascii_lowercase().as_str() {
                "content-length" => length = v.parse().unwrap_or(0).min(64 * 1024),
                "origin" => origin = Some(v),
                _ => {}
            }
        }
    }
    let mut body = vec![0u8; length];
    reader.read_exact(&mut body)?;
    Ok(Request { method, path, origin, body: String::from_utf8_lossy(&body).into_owned() })
}

fn respond(mut stream: TcpStream, code: u16, kind: &str, body: &[u8], extra: &str) -> std::io::Result<()> {
    let reason = match code {
        200 => "OK",
        302 => "Found",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        409 => "Conflict",
        _ => "Error",
    };
    write!(
        stream,
        "HTTP/1.1 {code} {reason}\r\nContent-Type: {kind}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n{extra}\r\n",
        body.len()
    )?;
    stream.write_all(body)
}

fn json(stream: TcpStream, code: u16, value: serde_json::Value) -> std::io::Result<()> {
    respond(stream, code, "application/json", value.to_string().as_bytes(), "")
}

fn handle(stream: TcpStream, app: &AppHandle, board: &Board) -> std::io::Result<()> {
    let req = read_request(&stream)?;
    let path = req.path.split('?').next().unwrap_or("/").to_string();
    if req.method == "POST" {
        if let Some(o) = &req.origin {
            if !ORIGINS.contains(&o.as_str()) {
                return json(stream, 403, serde_json::json!({"error": "origin not allowed"}));
            }
        }
        return match path.as_str() {
            "/send" => match board.send(&req.body) {
                Ok(()) => json(stream, 200, serde_json::json!({"ok": true})),
                Err("one line required") => json(stream, 400, serde_json::json!({"error": "one line required"})),
                Err(e) => json(stream, 409, serde_json::json!({"error": e})),
            },
            "/release" => {
                board.release();
                json(stream, 200, serde_json::json!({"ok": true}))
            }
            "/acquire" => {
                board.acquire();
                json(stream, 200, serde_json::json!({"ok": true}))
            }
            _ => json(stream, 404, serde_json::json!({"error": "not found"})),
        };
    }
    match path.as_str() {
        "/events" => events(stream, board),
        "/status" => {
            let s = board.status();
            json(stream, 200, serde_json::json!({"port": s.port, "open": s.state == "open", "released": s.released, "clients": s.clients}))
        }
        "/" if !req.path.contains('?') => respond(stream, 302, "text/plain", b"", "Location: /?bridge\r\n"),
        _ => {
            let file = if path == "/" { "index.html".to_string() } else { path.trim_start_matches('/').to_string() };
            match app.asset_resolver().get(file) {
                Some(asset) => respond(stream, 200, &asset.mime_type, &asset.bytes, ""),
                None => respond(stream, 404, "text/plain", b"not found", ""),
            }
        }
    }
}

fn events(mut stream: TcpStream, board: &Board) -> std::io::Result<()> {
    let rx = board.subscribe();
    stream.write_all(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n: connected\n\n")?;
    stream.flush()?;
    loop {
        match rx.recv_timeout(Duration::from_secs(15)) {
            Ok(line) => stream.write_all(format!("data: {line}\n\n").as_bytes())?,
            Err(RecvTimeoutError::Timeout) => stream.write_all(b": keepalive\n\n")?,
            Err(RecvTimeoutError::Disconnected) => return Ok(()),
        }
        stream.flush()?;
    }
}

/// Where the developer API keeps its copy of the board lines
pub fn log_path(app: &AppHandle) -> Option<std::path::PathBuf> {
    app.path().app_log_dir().ok().map(|d| d.join("board.log"))
}
