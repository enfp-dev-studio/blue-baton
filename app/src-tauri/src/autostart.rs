// SPDX-License-Identifier: GPL-3.0-only
//! Launch at login. On macOS a Launch Agent that also names the app's bundle identifier
//! (AssociatedBundleIdentifiers, see `man launchd.plist`): without it System Settings > Login Items cannot tie the
//! agent to the app and lists it under the bare executable (unsigned) or the signing certificate's name (signed).

use auto_launch::{AutoLaunch, AutoLaunchBuilder};
use tauri::{AppHandle, Manager};

struct Launcher(AutoLaunch);

/// Called once from setup
pub fn init(app: &AppHandle) -> Result<(), String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    #[cfg(target_os = "macos")]
    let exe = exe.canonicalize().map_err(|e| e.to_string())?;
    // An AppImage runs from a temporary mount, so the login entry has to start the AppImage file itself
    #[cfg(target_os = "linux")]
    let exe = app.env().appimage.unwrap_or(exe);
    let auto = AutoLaunchBuilder::new()
        // The same name the earlier autostart plugin used, so a login item from an older version is still found
        .set_app_name(&app.package_info().name)
        .set_app_path(&exe.display().to_string())
        .set_args(&[crate::HIDDEN_ARG])
        .set_bundle_identifiers(&[app.config().identifier.as_str()])
        .build()
        .map_err(|e| e.to_string())?;
    app.manage(Launcher(auto));
    Ok(())
}

pub fn is_enabled(app: &AppHandle) -> bool {
    app.try_state::<Launcher>().is_some_and(|l| l.0.is_enabled().unwrap_or(false))
}

pub fn set(app: &AppHandle, on: bool) -> Result<(), String> {
    let l = app.try_state::<Launcher>().ok_or("launch at login is not available")?;
    if on { l.0.enable() } else { l.0.disable() }.map_err(|e| e.to_string())
}
