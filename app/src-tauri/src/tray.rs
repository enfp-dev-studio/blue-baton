// SPDX-License-Identifier: GPL-3.0-only
//! Menu bar (macOS) / tray (Windows) icon and menu.
//!
//! Left click opens the panel under the icon (lib.rs, `toggle_panel`), like Google Drive; right click opens
//! this menu.
//!
//! The settings window keeps the board state (the UI store); it sends a small model here whenever the
//! hosts, the active host, or the input devices change, and the menu is rebuilt from it. Menu clicks go
//! back to the window as `tray-action` events, so all board logic stays in one place (the UI).
//! Labels come from the UI too, so the menu follows the UI language.

use serde::{Deserialize, Serialize};
use tauri::image::Image;
use tauri::menu::{CheckMenuItem, Menu, MenuItem, PredefinedMenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Emitter, Wry};
use tauri_plugin_autostart::ManagerExt;

pub const TRAY_ID: &str = "main";

#[derive(Deserialize, Clone, Default)]
pub struct HostItem {
    pub n: u8,
    /// What the host is called: the name the user gave it, otherwise its number
    #[serde(default)]
    pub label: String,
    pub name: String,
    /// ready | connected | secured | pairing | offline | empty
    pub state: String,
}

#[derive(Deserialize, Clone, Default)]
pub struct InputItem {
    pub name: String,
    pub state: String,
    pub ready: bool,
}

#[derive(Deserialize, Clone)]
#[serde(default)]
pub struct Labels {
    pub open: String,
    pub add_host: String,
    pub autostart: String,
    pub dev_api: String,
    pub quit: String,
    pub empty: String,
    pub offline: String,
    pub inputs: String,
    pub no_inputs: String,
    pub board_off: String,
    pub check_updates: String,
    /// "{version}" is replaced
    pub install_update: String,
    pub update_ready: String,
    pub up_to_date: String,
    pub update_failed: String,
}

impl Default for Labels {
    fn default() -> Self {
        Labels {
            open: "Open Blue Baton…".into(),
            add_host: "Add Host…".into(),
            autostart: "Launch at Login".into(),
            dev_api: "Developer API (127.0.0.1:8766)".into(),
            quit: "Quit Blue Baton".into(),
            empty: "Empty".into(),
            offline: "Offline".into(),
            inputs: "Input devices".into(),
            no_inputs: "No input device connected".into(),
            board_off: "Board not connected".into(),
            check_updates: "Check for Updates…".into(),
            install_update: "Install Update {version} and Restart".into(),
            update_ready: "Blue Baton {version} is available".into(),
            up_to_date: "Blue Baton is up to date".into(),
            update_failed: "Couldn't update Blue Baton. Try again later".into(),
        }
    }
}

#[derive(Deserialize, Clone, Default)]
pub struct Model {
    /// 1-3
    pub active: u8,
    /// PNG: the active host's name card for the macOS menu bar, 36 px high (drawn by the UI, controller.js)
    #[serde(default)]
    pub card: Vec<u8>,
    pub hosts: Vec<HostItem>,
    pub inputs: Vec<InputItem>,
    /// connected | disconnected
    pub board: String,
    pub labels: Labels,
    pub tooltip: String,
}

#[derive(Serialize, Clone)]
pub struct Action {
    pub kind: &'static str,
    pub host: u8,
}

fn tray_icon() -> tauri::Result<Image<'static>> {
    Image::from_bytes(include_bytes!("../icons/tray.png"))
}

/// macOS menu bar: the baton (without its margin) and the active host's name card side by side, both 36 px high.
/// Faded (a third of the alpha) while that host is not connected
#[cfg(target_os = "macos")]
fn card_icon(card: &[u8], faded: bool) -> tauri::Result<Image<'static>> {
    const GAP: usize = 5;
    let baton = Image::from_bytes(include_bytes!("../icons/tray-tight.png"))?;
    let card = Image::from_bytes(card)?;
    let (bw, cw, h) = (baton.width() as usize, card.width() as usize, baton.height() as usize);
    let w = bw + GAP + cw;
    let mut px = vec![0u8; w * h * 4];
    for (img, x0, iw) in [(&baton, 0, bw), (&card, bw + GAP, cw)] {
        let src = img.rgba();
        for y in 0..h.min(img.height() as usize) {
            px[(y * w + x0) * 4..(y * w + x0 + iw) * 4].copy_from_slice(&src[y * iw * 4..(y + 1) * iw * 4]);
        }
    }
    if faded {
        for a in px.iter_mut().skip(3).step_by(4) {
            *a /= 3;
        }
    }
    Ok(Image::new_owned(px, w as u32, h as u32))
}

pub fn create(app: &AppHandle, dev_api_on: bool) -> tauri::Result<()> {
    let menu = build_menu(app, &Model { board: "disconnected".into(), ..Default::default() }, dev_api_on)?;
    TrayIconBuilder::with_id(TRAY_ID)
        .icon(tray_icon()?)
        .icon_as_template(true)
        .tooltip("Blue Baton")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| on_menu(app, event.id().as_ref()))
        .on_tray_icon_event(|tray, event| {
            // The positioner needs the icon's position to place the panel under it
            tauri_plugin_positioner::on_tray_event(tray.app_handle(), &event);
            if let TrayIconEvent::Click { button: MouseButton::Left, button_state: MouseButtonState::Up, .. } = event {
                crate::toggle_panel(tray.app_handle());
            }
        })
        .build(app)?;
    Ok(())
}

pub fn update(app: &AppHandle, model: &Model, dev_api_on: bool) -> tauri::Result<()> {
    let Some(tray) = app.tray_by_id(TRAY_ID) else { return Ok(()) };
    tray.set_menu(Some(build_menu(app, model, dev_api_on)?))?;
    // macOS: the baton and a card with the name (or number) of the host that gets the input, faded while that host
    // is not connected; just the baton while the board is not
    #[cfg(target_os = "macos")]
    {
        let ready = model.hosts.iter().any(|h| h.n == model.active && h.state == "ready");
        let icon = if model.board == "connected" && !model.card.is_empty() {
            card_icon(&model.card, !ready)?
        } else {
            tray_icon()?
        };
        tray.set_icon(Some(icon))?;
        tray.set_icon_as_template(true)?;
    }
    tray.set_tooltip(Some(if model.tooltip.is_empty() { "Blue Baton".to_string() } else { model.tooltip.clone() }))?;
    Ok(())
}

fn host_label(model: &Model, n: u8) -> String {
    match model.hosts.iter().find(|h| h.n == n) {
        Some(h) if !h.label.is_empty() => h.label.clone(),
        _ => n.to_string(),
    }
}

fn build_menu(app: &AppHandle, model: &Model, dev_api_on: bool) -> tauri::Result<Menu<Wry>> {
    let l = &model.labels;
    let menu = Menu::new(app)?;
    let connected = model.board == "connected";
    if !connected {
        menu.append(&MenuItem::with_id(app, "status", &l.board_off, false, None::<&str>)?)?;
    }
    if connected {
        for n in 1..=3u8 {
            let host = model.hosts.iter().find(|h| h.n == n);
            let tag = host_label(model, n);
            let (label, enabled) = match host {
                Some(h) if h.state == "empty" => (format!("{tag}   {}", l.empty), false),
                Some(h) if h.state == "ready" => (format!("{tag}   {}", h.name), true),
                Some(h) => (format!("{tag}   {}  ·  {}", h.name, l.offline), false),
                None => (format!("{tag}   {}", l.empty), false),
            };
            let item = CheckMenuItem::with_id(app, format!("host-{n}"), label, enabled, model.active == n, None::<&str>)?;
            menu.append(&item)?;
        }
        menu.append(&PredefinedMenuItem::separator(app)?)?;
        menu.append(&MenuItem::with_id(app, "inputs-title", &l.inputs, false, None::<&str>)?)?;
        let live: Vec<&InputItem> = model.inputs.iter().collect();
        if live.is_empty() {
            menu.append(&MenuItem::with_id(app, "inputs-none", &l.no_inputs, false, None::<&str>)?)?;
        }
        for (i, d) in live.iter().enumerate() {
            let mark = if d.ready { "●" } else { "○" };
            let text = format!("{mark}  {}  ·  {}", d.name, d.state);
            menu.append(&MenuItem::with_id(app, format!("input-{i}"), text, false, None::<&str>)?)?;
        }
    }
    menu.append(&PredefinedMenuItem::separator(app)?)?;
    menu.append(&MenuItem::with_id(app, "open", &l.open, true, None::<&str>)?)?;
    menu.append(&MenuItem::with_id(app, "add-host", &l.add_host, connected, None::<&str>)?)?;
    menu.append(&PredefinedMenuItem::separator(app)?)?;
    let autostart = app.autolaunch().is_enabled().unwrap_or(false);
    menu.append(&CheckMenuItem::with_id(app, "autostart", &l.autostart, true, autostart, None::<&str>)?)?;
    menu.append(&CheckMenuItem::with_id(app, "devapi", &l.dev_api, true, dev_api_on, None::<&str>)?)?;
    menu.append(&PredefinedMenuItem::separator(app)?)?;
    if let Some(version) = crate::updates::pending_version(app) {
        menu.append(&MenuItem::with_id(app, "install-update", l.install_update.replace("{version}", &version), true, None::<&str>)?)?;
    }
    menu.append(&MenuItem::with_id(app, "check-updates", &l.check_updates, true, None::<&str>)?)?;
    menu.append(&MenuItem::with_id(app, "quit", &l.quit, true, None::<&str>)?)?;
    Ok(menu)
}

fn on_menu(app: &AppHandle, id: &str) {
    match id {
        "open" => crate::show_main(app),
        "add-host" => {
            crate::show_main(app);
            let _ = app.emit("tray-action", Action { kind: "add_host", host: 0 });
        }
        "autostart" => {
            let auto = app.autolaunch();
            let _ = if auto.is_enabled().unwrap_or(false) { auto.disable() } else { auto.enable() };
            crate::refresh_tray(app);
        }
        "devapi" => crate::toggle_dev_api(app),
        "check-updates" => crate::updates::check_now(app),
        "install-update" => crate::updates::install(app),
        "quit" => app.exit(0),
        _ => {
            if let Some(n) = id.strip_prefix("host-").and_then(|s| s.parse::<u8>().ok()) {
                let _ = app.emit("tray-action", Action { kind: "select_host", host: n });
                // The menu shows the new choice when the UI confirms it (tray_update)
                crate::refresh_tray(app);
            }
        }
    }
}

#[cfg(all(test, target_os = "macos"))]
mod tests {
    use super::*;

    #[test]
    fn card_icon_puts_the_card_after_the_baton() {
        // Any 36 px high PNG will do as the card: the baton itself (37 x 36, drawn in the middle)
        let png = include_bytes!("../icons/tray-tight.png");
        let img = card_icon(png, false).unwrap();
        assert_eq!((img.width(), img.height()), (37 + 5 + 37, 36));
        let alpha = |x: usize, y: usize| img.rgba()[(y * img.width() as usize + x) * 4 + 3];
        assert_eq!(alpha(18, 18), 255, "baton");
        assert_eq!(alpha(39, 18), 0, "gap");
        assert_eq!(alpha(42 + 18, 18), 255, "card");
        let faded = card_icon(png, true).unwrap();
        assert_eq!(faded.rgba()[(18 * faded.width() as usize + 60) * 4 + 3], 85, "faded to a third");
    }
}
