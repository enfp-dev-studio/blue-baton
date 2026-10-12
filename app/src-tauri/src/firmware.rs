// SPDX-License-Identifier: GPL-3.0-only
//! The firmware that comes with the app, and writing it to the board over USB (espflash).
//!
//! A release build carries the firmware of the same release (build.rs), so updating the app brings the firmware
//! with it: the settings window offers it when the board runs an older version, or none of ours. Two ways to write:
//! - Update: bootloader and partition table (0x0-0x9000) and the app (0x20000-). The pairing store between them
//!   (nvs at 0x9000, firmware/partitions.csv) is left alone, so input devices and computers stay paired.
//! - Install: the whole image from 0x0, which also clears the pairing store. For a board with other firmware.
//!
//! The board link lets go of the port while the flasher has it (Board::release) and finds the board again after.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;

use espflash::connection::{Connection, ResetAfterOperation, ResetBeforeOperation};
use espflash::flasher::Flasher;
use espflash::image_format::Segment;
use espflash::target::{Chip, ProgressCallbacks};
use serde::Serialize;
use serialport::{FlowControl, SerialPortType};
use tauri::{AppHandle, Emitter};

use crate::board::Board;

/// Where the pairing store starts and the app starts (firmware/partitions.csv)
const NVS_START: usize = 0x9000;
const APP_START: usize = 0x20000;

#[cfg(bundled_firmware)]
static IMAGE: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/firmware/blue-baton-esp32s3.bin"));
#[cfg(not(bundled_firmware))]
static IMAGE: &[u8] = &[];

static WRITING: AtomicBool = AtomicBool::new(false);

/// The version of the firmware in this build, if it carries one (a development build may not)
pub fn bundled_version() -> Option<&'static str> {
    option_env!("BB_FIRMWARE_VERSION").filter(|_| IMAGE.len() > APP_START)
}

/// Sent to the window as `firmware-progress`
#[derive(Clone, Serialize)]
struct Progress {
    /// connecting | writing | done | failed
    stage: &'static str,
    percent: u32,
    error: Option<String>,
}

/// Write the bundled firmware to the board on its port, in the background. `keep_pairings`: update (true) or
/// install (false)
pub fn start(app: AppHandle, board: Arc<Board>, keep_pairings: bool) -> Result<(), String> {
    if bundled_version().is_none() {
        return Err("this build has no firmware".into());
    }
    let port = board.status().port.ok_or("no board")?;
    if WRITING.swap(true, Ordering::SeqCst) {
        return Err("already writing".into());
    }
    thread::spawn(move || {
        let emit = |stage, percent, error| {
            let _ = app.emit("firmware-progress", Progress { stage, percent, error });
        };
        emit("connecting", 0, None);
        board.release();
        let result = write(&port, keep_pairings, |p| emit("writing", p, None));
        board.acquire(); // the board restarts into the new firmware and is found again
        match result {
            Ok(()) => emit("done", 100, None),
            Err(e) => emit("failed", 0, Some(e)),
        }
        WRITING.store(false, Ordering::SeqCst);
    });
    Ok(())
}

fn write(port: &str, keep_pairings: bool, on_percent: impl FnMut(u32)) -> Result<(), String> {
    let info = serialport::available_ports()
        .map_err(|e| e.to_string())?
        .into_iter()
        .find(|p| p.port_name == port)
        .and_then(|p| match p.port_type {
            SerialPortType::UsbPort(u) => Some(u),
            _ => None,
        })
        .ok_or("the board's port is gone")?;
    let serial = serialport::new(port, 115_200).flow_control(FlowControl::None).open_native().map_err(|e| e.to_string())?;
    let connection =
        Connection::new(serial, info, ResetAfterOperation::HardReset, ResetBeforeOperation::DefaultReset, 115_200);
    let mut flasher = Flasher::connect(connection, true, true, false, Some(Chip::Esp32s3), None).map_err(|e| e.to_string())?;
    let segments = segments(keep_pairings);
    let mut progress = Percent::new(&segments, on_percent);
    flasher.write_bins_to_flash(&segments, &mut progress).map_err(|e| e.to_string())?;
    flasher.connection().reset_after(true, Chip::Esp32s3).map_err(|e| e.to_string())
}

fn segments(keep_pairings: bool) -> Vec<Segment<'static>> {
    if keep_pairings {
        vec![Segment::new(0, &IMAGE[..NVS_START]), Segment::new(APP_START as u32, &IMAGE[APP_START..])]
    } else {
        vec![Segment::new(0, IMAGE)]
    }
}

/// espflash reports chunks per segment; this turns that into one percentage over all the bytes written
struct Percent<F: FnMut(u32)> {
    sizes: Vec<usize>,
    total: usize,
    done: usize,
    current: usize,
    chunks: usize,
    last: u32,
    on_percent: F,
}

impl<F: FnMut(u32)> Percent<F> {
    fn new(segments: &[Segment], on_percent: F) -> Self {
        let sizes: Vec<usize> = segments.iter().map(|s| s.data.len()).collect();
        let total = sizes.iter().sum::<usize>().max(1);
        Percent { sizes, total, done: 0, current: 0, chunks: 1, last: u32::MAX, on_percent }
    }
}

impl<F: FnMut(u32)> ProgressCallbacks for Percent<F> {
    fn init(&mut self, _addr: u32, chunks: usize) {
        self.chunks = chunks.max(1);
    }

    fn update(&mut self, chunk: usize) {
        let size = self.sizes.get(self.current).copied().unwrap_or(0);
        let written = self.done + size * chunk.min(self.chunks) / self.chunks;
        let percent = (written * 100 / self.total) as u32;
        if percent != self.last {
            self.last = percent;
            (self.on_percent)(percent);
        }
    }

    fn verifying(&mut self) {}

    fn finish(&mut self, _skipped: bool) {
        self.done += self.sizes.get(self.current).copied().unwrap_or(0);
        self.current += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn percent_runs_over_all_segments() {
        let a = [0u8; 300];
        let b = [0u8; 100];
        let segments = [Segment::new(0, &a), Segment::new(0x1000, &b)];
        let mut seen = Vec::new();
        let mut p = Percent::new(&segments, |v| seen.push(v));
        p.init(0, 3);
        (1..=3).for_each(|c| p.update(c));
        p.finish(false);
        p.init(0x1000, 2);
        (1..=2).for_each(|c| p.update(c));
        p.finish(false);
        assert_eq!(seen, [25, 50, 75, 87, 100]);
    }

    /// On a real board, with nothing else holding its port: BB_TEST_PORT=/dev/cu.usbmodem101 cargo test -- --ignored
    #[test]
    #[ignore]
    fn update_a_real_board() {
        let port = std::env::var("BB_TEST_PORT").expect("BB_TEST_PORT");
        let mut seen = Vec::new();
        write(&port, true, |p| seen.push(p)).unwrap();
        assert_eq!(seen.last(), Some(&100));
    }

    #[test]
    fn update_leaves_the_pairing_store() {
        if bundled_version().is_none() {
            return; // a build without firmware
        }
        let s = segments(true);
        assert_eq!((s[0].addr, s[0].data.len()), (0, NVS_START));
        assert_eq!(s[1].addr as usize, APP_START);
        assert_eq!(s[1].data.len(), IMAGE.len() - APP_START);
        assert_eq!(segments(false)[0].data.len(), IMAGE.len());
    }
}
