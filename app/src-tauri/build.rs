// SPDX-License-Identifier: GPL-3.0-only
//! Bundles the firmware image when one is in firmware/ (release builds put the same release's firmware there; see
//! firmware.rs). Its version comes from firmware/main/version.h, the same source the firmware reports in `hello`.

use std::path::Path;

const IMAGE: &str = "firmware/blue-baton-esp32s3.bin";
const VERSION_H: &str = "../../firmware/main/version.h";

fn main() {
    println!("cargo:rustc-check-cfg=cfg(bundled_firmware)");
    println!("cargo:rerun-if-changed={IMAGE}");
    println!("cargo:rerun-if-changed={VERSION_H}");
    if Path::new(IMAGE).exists() {
        let header = std::fs::read_to_string(VERSION_H).expect("firmware/main/version.h");
        let version = header
            .lines()
            .find_map(|l| l.trim().strip_prefix("#define KM_FW_VERSION "))
            .map(|v| v.trim().trim_matches('"').to_string())
            .expect("KM_FW_VERSION in version.h");
        println!("cargo:rustc-cfg=bundled_firmware");
        println!("cargo:rustc-env=BB_FIRMWARE_VERSION={version}");
    }
    tauri_build::build()
}
