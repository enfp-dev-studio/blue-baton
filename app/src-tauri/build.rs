// SPDX-License-Identifier: GPL-3.0-only
//! Bundles the firmware image when one is in firmware/ (release builds put the same release's firmware there; see
//! firmware.rs). Its version is read from the image itself (the app description ESP-IDF puts at the start of the
//! app, from `git describe` at build time), so the app offers exactly what it carries: a stale image left in a
//! development checkout was once offered as the current version and wrote an older firmware.

use std::path::Path;

const IMAGE: &str = "firmware/blue-baton-esp32s3.bin";
/// esp_app_desc_t: right after the image and first segment headers at the app's start (0x20000, partitions.csv)
const APP_DESC: usize = 0x20000 + 0x20;
const APP_DESC_MAGIC: u32 = 0xABCD_5432;

/// "v0.7.6", "v0.7.6-dirty" or "v0.7.6-3-gabc1234" gives "0.7.6"
fn image_version(image: &[u8]) -> Option<String> {
    let desc = image.get(APP_DESC..APP_DESC + 48)?;
    if u32::from_le_bytes(desc[0..4].try_into().ok()?) != APP_DESC_MAGIC {
        return None;
    }
    let field = &desc[16..48];
    let text = std::str::from_utf8(&field[..field.iter().position(|&b| b == 0).unwrap_or(32)]).ok()?;
    let version = text.trim_start_matches('v').split('-').next()?;
    (version.split('.').count() == 3 && version.split('.').all(|p| p.parse::<u32>().is_ok())).then(|| version.to_string())
}

fn main() {
    println!("cargo:rustc-check-cfg=cfg(bundled_firmware)");
    println!("cargo:rerun-if-changed={IMAGE}");
    if let Ok(image) = std::fs::read(Path::new(IMAGE)) {
        let version = image_version(&image).expect("firmware/blue-baton-esp32s3.bin: no app description with a version");
        println!("cargo:rustc-cfg=bundled_firmware");
        println!("cargo:rustc-env=BB_FIRMWARE_VERSION={version}");
    }
    tauri_build::build()
}
