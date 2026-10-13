// SPDX-License-Identifier: GPL-3.0-only
//! Blue Baton menu bar app.
//!
//! - Owns the board's USB serial port (board.rs) and hands lines to the settings window.
//! - Menu bar / tray icon with the active host: left click opens a panel under it (quick switching),
//!   right click a menu (tray.rs). No Dock icon (LSUIElement in Info.plist).
//! - The settings window is the shared UI (../../ui); closing it only hides it.
//! - Optional developer API compatible with tools/bridge.py (devapi.rs).

mod board;
mod devapi;
mod firmware;
mod updates;
mod tray;

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, WindowEvent};
use tauri_plugin_autostart::{MacosLauncher, ManagerExt};
use tauri_plugin_notification::NotificationExt;
use tauri_plugin_positioner::{Position, WindowExt};

/// Passed by the login item so the app starts in the menu bar without opening the window
const HIDDEN_ARG: &str = "--hidden";

#[derive(Serialize, Deserialize, Default, Clone)]
struct Settings {
    dev_api: bool,
}

pub struct AppState {
    board: Arc<board::Board>,
    dev_api: Mutex<Option<devapi::DevApi>>,
    settings: Mutex<Settings>,
    tray_model: Mutex<tray::Model>,
}

fn settings_path(app: &AppHandle) -> Option<std::path::PathBuf> {
    app.path().app_config_dir().ok().map(|d| d.join("settings.json"))
}

fn load_settings(app: &AppHandle) -> Settings {
    settings_path(app)
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or_default()
}

fn save_settings(app: &AppHandle, s: &Settings) {
    if let Some(p) = settings_path(app) {
        if let Some(dir) = p.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let _ = std::fs::write(p, serde_json::to_string_pretty(s).unwrap_or_default());
    }
}

/// The settings window. The app stays out of the Dock even then (LSUIElement, like Google Drive)
pub(crate) fn show_main(app: &AppHandle) {
    hide_panel(app);
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
    }
}

fn hide_main(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.hide();
    }
}

/* ---- the panel under the menu bar icon ---- */

/// When the panel last hid because it lost focus. Clicking the icon while the panel is open first takes the
/// focus away (the panel hides) and then delivers the click; without this the click would reopen it at once
static PANEL_HIDDEN_AT: AtomicU64 = AtomicU64::new(0);

fn now_ms() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_millis() as u64).unwrap_or(0)
}

pub(crate) fn hide_panel(app: &AppHandle) {
    if let Some(p) = app.get_webview_window("panel") {
        if p.is_visible().unwrap_or(false) {
            let _ = p.hide();
            PANEL_HIDDEN_AT.store(now_ms(), Ordering::SeqCst);
        }
    }
}

pub(crate) fn toggle_panel(app: &AppHandle) {
    let Some(p) = app.get_webview_window("panel") else { return };
    if p.is_visible().unwrap_or(false) {
        hide_panel(app);
        return;
    }
    if now_ms().saturating_sub(PANEL_HIDDEN_AT.load(Ordering::SeqCst)) < 300 {
        return;
    }
    // Under the icon on macOS (menu bar at the top); above it on Windows (taskbar at the bottom)
    #[cfg(target_os = "macos")]
    let _ = p.move_window(Position::TrayBottomCenter);
    #[cfg(not(target_os = "macos"))]
    let _ = p.move_window(Position::TrayCenter);
    let _ = p.show();
    let _ = p.set_focus();
}

/// Menu and notification labels in the UI language (sent with the tray model)
pub(crate) fn tray_labels(app: &AppHandle) -> tray::Labels {
    app.state::<AppState>().tray_model.lock().unwrap().labels.clone()
}

pub(crate) fn refresh_tray(app: &AppHandle) {
    let state = app.state::<AppState>();
    let model = state.tray_model.lock().unwrap().clone();
    let dev = state.settings.lock().unwrap().dev_api;
    let _ = tray::update(app, &model, dev);
}

fn set_dev_api(app: &AppHandle, on: bool) {
    let state = app.state::<AppState>();
    let mut slot = state.dev_api.lock().unwrap();
    if let Some(api) = slot.take() {
        api.stop();
    }
    state.board.set_log(None);
    if on {
        match devapi::DevApi::start(app.clone(), state.board.clone()) {
            Ok(api) => {
                *slot = Some(api);
                state.board.set_log(devapi::log_path(app).as_deref());
            }
            Err(e) => {
                // Most likely tools/bridge.py is already listening on the port
                let _ = app.notification().builder().title("Blue Baton").body(format!("Developer API: {e}")).show();
            }
        }
    }
}

pub(crate) fn toggle_dev_api(app: &AppHandle) {
    let on = {
        let state = app.state::<AppState>();
        let mut s = state.settings.lock().unwrap();
        s.dev_api = !s.dev_api;
        save_settings(app, &s);
        s.dev_api
    };
    set_dev_api(app, on);
    refresh_tray(app);
}

/* ---- commands for the settings window ---- */

#[tauri::command]
fn board_status(state: tauri::State<AppState>) -> board::Status {
    state.board.status()
}

#[tauri::command]
fn board_send(state: tauri::State<AppState>, line: String) -> Result<(), String> {
    state.board.send(&line).map_err(|e| e.to_string())
}

/// The firmware version this app carries (None in a development build without one)
#[tauri::command]
fn firmware_bundled() -> Option<&'static str> {
    firmware::bundled_version()
}

/// Write the bundled firmware to the board: update keeps pairings, install clears them. Progress comes as
/// `firmware-progress` events
#[tauri::command]
fn firmware_write(app: AppHandle, state: tauri::State<AppState>, keep_pairings: bool) -> Result<(), String> {
    firmware::start(app, state.board.clone(), keep_pairings)
}

/// Launch at login, for the settings window (the tray menu has the same switch)
#[tauri::command]
fn autostart_get(app: AppHandle) -> bool {
    app.autolaunch().is_enabled().unwrap_or(false)
}

#[tauri::command]
fn autostart_set(app: AppHandle, on: bool) -> Result<(), String> {
    let launcher = app.autolaunch();
    if on { launcher.enable() } else { launcher.disable() }.map_err(|e| e.to_string())?;
    refresh_tray(&app);
    Ok(())
}

#[tauri::command]
fn tray_update(app: AppHandle, state: tauri::State<AppState>, model: tray::Model) {
    *state.tray_model.lock().unwrap() = model;
    refresh_tray(&app);
}

#[tauri::command]
fn window_show(app: AppHandle) {
    show_main(&app);
}

#[tauri::command]
fn panel_hide(app: AppHandle) {
    hide_panel(&app);
}

#[tauri::command]
fn app_quit(app: AppHandle) {
    app.exit(0);
}

#[tauri::command]
fn notify(app: AppHandle, title: String, body: String) {
    let _ = app.notification().builder().title(title).body(body).show();
}

pub fn run() {
    tauri::Builder::default()
        // First, so a second launch only brings the running app forward (one owner for the serial port)
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| show_main(app)))
        .plugin(tauri_plugin_autostart::init(MacosLauncher::LaunchAgent, Some(vec![HIDDEN_ARG])))
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_positioner::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .setup(|app| {
            let handle = app.handle().clone();
            #[cfg(target_os = "macos")]
            app.set_activation_policy(tauri::ActivationPolicy::Accessory);

            let settings = load_settings(&handle);
            let board = board::Board::start(handle.clone());
            app.manage(updates::Pending::default());
            app.manage(AppState {
                board,
                dev_api: Mutex::new(None),
                settings: Mutex::new(settings.clone()),
                tray_model: Mutex::new(tray::Model { board: "disconnected".into(), ..Default::default() }),
            });
            if settings.dev_api {
                set_dev_api(&handle, true);
            }
            tray::create(&handle, settings.dev_api)?;
            updates::start(&handle);

            if let Some(w) = app.get_webview_window("main") {
                let h = handle.clone();
                w.on_window_event(move |e| {
                    if let WindowEvent::CloseRequested { api, .. } = e {
                        api.prevent_close();
                        hide_main(&h);
                    }
                });
            }
            if let Some(p) = app.get_webview_window("panel") {
                let h = handle.clone();
                p.on_window_event(move |e| {
                    // Like a menu: it closes when you click anywhere else
                    if let WindowEvent::Focused(false) = e {
                        hide_panel(&h);
                    }
                });
            }
            if !std::env::args().any(|a| a == HIDDEN_ARG) {
                show_main(&handle);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            board_status, board_send, firmware_bundled, firmware_write, autostart_get, autostart_set, tray_update,
            window_show, panel_hide, app_quit, notify
        ])
        .build(tauri::generate_context!())
        .expect("error while building Blue Baton")
        .run(|app, event| {
            // Keep running in the menu bar when the window is closed
            if let tauri::RunEvent::ExitRequested { api, code: None, .. } = event {
                api.prevent_exit();
                let _ = app;
            }
        });
}
