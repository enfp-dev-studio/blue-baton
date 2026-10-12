//! Updates from GitHub Releases (tauri-plugin-updater). The release workflow publishes `latest.json` and the update
//! files signed with the project's update key; the app checks a minute after it starts and then once a day, and
//! "Check for Updates…" checks at once. A found update adds "Install Update x.y.z and Restart" to the menu and shows
//! a notification once per version. Installing replaces the app and restarts it.

use std::sync::Mutex;
use std::time::Duration;

use tauri::{AppHandle, Manager};
use tauri_plugin_notification::NotificationExt;
use tauri_plugin_updater::{Update, UpdaterExt};

/// The update found by the last check, until it is installed
#[derive(Default)]
pub struct Pending(pub Mutex<Option<Update>>);

pub fn pending_version(app: &AppHandle) -> Option<String> {
    app.state::<Pending>().0.lock().ok()?.as_ref().map(|u| u.version.clone())
}

pub fn start(app: &AppHandle) {
    let app = app.clone();
    std::thread::spawn(move || {
        std::thread::sleep(Duration::from_secs(60));
        loop {
            tauri::async_runtime::block_on(check(&app, false));
            std::thread::sleep(Duration::from_secs(24 * 3600));
        }
    });
}

pub fn check_now(app: &AppHandle) {
    let app = app.clone();
    tauri::async_runtime::spawn(async move { check(&app, true).await });
}

pub fn install(app: &AppHandle) {
    let Some(update) = app.state::<Pending>().0.lock().ok().and_then(|mut p| p.take()) else { return };
    let app = app.clone();
    tauri::async_runtime::spawn(async move {
        match update.download_and_install(|_, _| {}, || {}).await {
            Ok(()) => app.restart(),
            Err(e) => {
                eprintln!("update install failed: {e}");
                let l = crate::tray_labels(&app);
                notify(&app, &l.update_failed);
                crate::refresh_tray(&app);
            }
        }
    });
}

async fn check(app: &AppHandle, manual: bool) {
    let result = match app.updater() {
        Ok(updater) => updater.check().await,
        Err(e) => Err(e),
    };
    let l = crate::tray_labels(app);
    match result {
        Ok(Some(update)) => {
            let version = update.version.clone();
            let new = pending_version(app).as_deref() != Some(version.as_str());
            if let Ok(mut p) = app.state::<Pending>().0.lock() {
                *p = Some(update);
            }
            crate::refresh_tray(app);
            if new || manual {
                notify(app, &l.update_ready.replace("{version}", &version));
            }
        }
        Ok(None) => {
            if manual {
                notify(app, &l.up_to_date);
            }
        }
        Err(e) => {
            eprintln!("update check failed: {e}");
            if manual {
                notify(app, &l.update_failed);
            }
        }
    }
}

fn notify(app: &AppHandle, body: &str) {
    let _ = app.notification().builder().title("Blue Baton").body(body).show();
}
