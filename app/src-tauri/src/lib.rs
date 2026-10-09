//! Blue Baton menu bar app.
//!
//! - Owns the board's USB serial port (board.rs) and hands lines to the settings window.
//! - Menu bar / tray icon with the active host and quick switching (tray.rs).
//! - The settings window is the shared UI (../../ui); closing it only hides it.
//! - Remembers the settings PIN in the system keychain so the tray works without unlocking each time.
//! - Optional developer API compatible with tools/bridge.py (devapi.rs).

mod board;
mod devapi;
mod tray;

use std::sync::{Arc, Mutex};

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, WindowEvent};
use tauri_plugin_autostart::MacosLauncher;
use tauri_plugin_notification::NotificationExt;

const KEYCHAIN_SERVICE: &str = "com.enfpdev.bluebaton";
const KEYCHAIN_PIN: &str = "board-pin";
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

pub(crate) fn show_main(app: &AppHandle) {
    // A menu bar app has no Dock icon; it gets one while its window is open, like other menu bar utilities
    #[cfg(target_os = "macos")]
    let _ = app.set_activation_policy(tauri::ActivationPolicy::Regular);
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
    #[cfg(target_os = "macos")]
    let _ = app.set_activation_policy(tauri::ActivationPolicy::Accessory);
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
                if let Some(path) = devapi::log_path(app) {
                    state.board.set_log(board::Board::open_log(&path));
                }
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
fn notify(app: AppHandle, title: String, body: String) {
    let _ = app.notification().builder().title(title).body(body).show();
}

fn pin_entry() -> Option<keyring::Entry> {
    keyring::Entry::new(KEYCHAIN_SERVICE, KEYCHAIN_PIN).ok()
}

#[tauri::command]
fn pin_load() -> Option<String> {
    pin_entry()?.get_password().ok()
}

#[tauri::command]
fn pin_save(pin: String) -> Result<(), String> {
    pin_entry().ok_or("keychain unavailable")?.set_password(&pin).map_err(|e| e.to_string())
}

#[tauri::command]
fn pin_clear() {
    if let Some(e) = pin_entry() {
        let _ = e.delete_credential();
    }
}

pub fn run() {
    tauri::Builder::default()
        // First, so a second launch only brings the running app forward (one owner for the serial port)
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| show_main(app)))
        .plugin(tauri_plugin_autostart::init(MacosLauncher::LaunchAgent, Some(vec![HIDDEN_ARG])))
        .plugin(tauri_plugin_notification::init())
        .setup(|app| {
            let handle = app.handle().clone();
            #[cfg(target_os = "macos")]
            app.set_activation_policy(tauri::ActivationPolicy::Accessory);

            let settings = load_settings(&handle);
            let board = board::Board::start(handle.clone());
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

            if let Some(w) = app.get_webview_window("main") {
                let h = handle.clone();
                w.on_window_event(move |e| {
                    if let WindowEvent::CloseRequested { api, .. } = e {
                        api.prevent_close();
                        hide_main(&h);
                    }
                });
            }
            if !std::env::args().any(|a| a == HIDDEN_ARG) {
                show_main(&handle);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            board_status, board_send, tray_update, window_show, notify, pin_load, pin_save, pin_clear
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
