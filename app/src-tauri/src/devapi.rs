//! Developer API on 127.0.0.1:8766, the same HTTP interface as firmware/tools/bridge.py, so the flash
//! helper (/release, /acquire) and monitoring tools keep working while the app owns the port.
//! Off by default (tray menu → Developer API), because board lines include typed passkeys.
//!
//!   GET  /events   board lines as server-sent events
//!   POST /send     one JSON command line
//!   POST /release  close the port for flashing (answers once it is closed); POST /acquire reopens it
//!   GET  /status   {"port", "open", "released", "clients"}
//!   GET  other     the settings UI (bundled assets), "/" → "/?bridge"
//!
//! Security: bound to 127.0.0.1 only. Every request must name this server in its Host header, so a site that
//! points its own DNS name at 127.0.0.1 gets nothing (DNS rebinding). POST is accepted only without an Origin
//! header (command line tools) or from this server's own origin. No CORS headers, so other sites cannot read
//! /events. Turning the API off also ends the open /events streams.

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
const HOSTS: [&str; 2] = ["127.0.0.1:8766", "localhost:8766"];
const ORIGINS: [&str; 2] = ["http://127.0.0.1:8766", "http://localhost:8766"];

pub struct DevApi {
    stop: Arc<AtomicBool>,
    board: Arc<Board>,
}

impl DevApi {
    pub fn start(app: AppHandle, board: Arc<Board>) -> std::io::Result<Self> {
        let listener = TcpListener::bind(ADDR)?;
        listener.set_nonblocking(true)?;
        let api = DevApi { stop: Arc::new(AtomicBool::new(false)), board: board.clone() };
        let flag = api.stop.clone();
        thread::Builder::new().name("devapi".into()).spawn(move || {
            while !flag.load(Ordering::SeqCst) {
                match listener.accept() {
                    Ok((stream, _)) => {
                        let (app, board, flag) = (app.clone(), board.clone(), flag.clone());
                        thread::spawn(move || {
                            let _ = handle(stream, &app, &board, &flag);
                        });
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => thread::sleep(Duration::from_millis(50)),
                    Err(_) => thread::sleep(Duration::from_millis(200)),
                }
            }
        })?;
        Ok(api)
    }

    pub fn stop(&self) {
        self.stop.store(true, Ordering::SeqCst);
        // Ends the open /events streams: their receivers see the senders go
        self.board.unsubscribe_all();
    }
}

struct Request {
    method: String,
    path: String,
    host: Option<String>,
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
    let (mut length, mut host, mut origin) = (0usize, None, None);
    loop {
        let mut line = String::new();
        if reader.read_line(&mut line)? == 0 || line == "\r\n" || line == "\n" {
            break;
        }
        if let Some((k, v)) = line.split_once(':') {
            let v = v.trim().to_string();
            match k.trim().to_ascii_lowercase().as_str() {
                "content-length" => length = v.parse().unwrap_or(0).min(64 * 1024),
                "host" => host = Some(v),
                "origin" => origin = Some(v),
                _ => {}
            }
        }
    }
    let mut body = vec![0u8; length];
    reader.read_exact(&mut body)?;
    Ok(Request { method, path, host, origin, body: String::from_utf8_lossy(&body).into_owned() })
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

fn host_allowed(host: Option<&str>) -> bool {
    host.is_some_and(|h| HOSTS.iter().any(|a| a.eq_ignore_ascii_case(h)))
}

fn handle(stream: TcpStream, app: &AppHandle, board: &Board, stop: &AtomicBool) -> std::io::Result<()> {
    let req = read_request(&stream)?;
    if !host_allowed(req.host.as_deref()) {
        return json(stream, 403, serde_json::json!({"error": "host not allowed"}));
    }
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
        "/events" => events(stream, board, stop),
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

fn events(mut stream: TcpStream, board: &Board, stop: &AtomicBool) -> std::io::Result<()> {
    let rx = board.subscribe();
    if stop.load(Ordering::SeqCst) {
        return Ok(()); // turned off while this request was on its way
    }
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_this_server_as_host() {
        assert!(host_allowed(Some("127.0.0.1:8766")));
        assert!(host_allowed(Some("localhost:8766")));
        assert!(host_allowed(Some("LOCALHOST:8766")));
        assert!(!host_allowed(Some("evil.example:8766")));
        assert!(!host_allowed(Some("127.0.0.1")));
        assert!(!host_allowed(Some("127.0.0.1:8766.evil.example")));
        assert!(!host_allowed(None));
    }

    #[test]
    fn reads_host_and_origin() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let mut client = TcpStream::connect(listener.local_addr().unwrap()).unwrap();
        client
            .write_all(b"POST /send HTTP/1.1\r\nHost: rebind.example:8766\r\nOrigin: http://rebind.example:8766\r\nContent-Length: 2\r\n\r\n{}")
            .unwrap();
        let (server, _) = listener.accept().unwrap();
        let req = read_request(&server).unwrap();
        assert_eq!(req.host.as_deref(), Some("rebind.example:8766"));
        assert_eq!(req.origin.as_deref(), Some("http://rebind.example:8766"));
        assert_eq!(req.body, "{}");
        assert!(!host_allowed(req.host.as_deref()));
    }
}
