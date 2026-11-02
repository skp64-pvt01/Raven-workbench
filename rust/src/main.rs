//! Raven Workbench (Rust) - a desktop application shell on Raven.
//!
//! A menu bar, a toolbar, a side bar, a status bar and four panes
//! (Application, Instrument panel, User guide, Preferences); a JSONC
//! configuration file with a JSON Schema that drives the Preferences form; an
//! embedded user guide and right-click menus.
//!
//! ```sh
//! RAVEN_LIB_DIR=/path/to/Raven/build cargo run --release                # the application
//! RAVEN_LIB_DIR=/path/to/Raven/build cargo run --release -- --test      # checks only
//! RAVEN_LIB_DIR=/path/to/Raven/build cargo run --release -- --capture shot.ppm
//! ```
//!
//! The settings come from a JSONC file (JSON with comments), found in this
//! order: `--config <path>`, the WORKBENCH_CONFIG variable, then the
//! `workbench.jsonc` next to the C++ and Python implementations.

mod config;
mod json;
mod panel;
mod preferences;
mod schema;

use config::Config;
use raven::*;
use schema::Schema;
use std::ffi::c_void;
use std::os::raw::c_int;

const VERSION: &str = env!("CARGO_PKG_VERSION");
const DEFAULT_CONFIG: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../workbench.jsonc");
const SCHEMA_PATH: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../workbench.schema.json");
const HELP_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../help");

const PANE_APPLICATION: i32 = 0;
const PANE_PANEL: i32 = 1;
const PANE_HELP: i32 = 2;
const PANE_PREFERENCES: i32 = 3;

/// The web view functions live in the same library as the rest of Raven, but
/// the generated Rust binding stops at `Raven.h`: the guide declares the
/// handful of calls it needs. They are C symbols of libraven, so nothing is vendored.
mod web {
    use raven::Rid;
    use std::os::raw::{c_char, c_int};

    extern "C" {
        pub fn Rweb_is_available() -> c_int;
        pub fn Rweb_set_browser_path(path: *const c_char);
        pub fn Rweb_view_create() -> Rid;
        pub fn Rweb_view_load_url(view: Rid, url: *const c_char);
        pub fn Rweb_view_reload(view: Rid);
        pub fn Rweb_view_go_back(view: Rid);
        pub fn Rweb_view_go_forward(view: Rid);
        pub fn Rweb_view_can_go_back(view: Rid) -> c_int;
        pub fn Rweb_view_can_go_forward(view: Rid) -> c_int;
        pub fn Rweb_view_get_title(view: Rid) -> *const c_char;
        pub fn Rweb_view_is_loading(view: Rid) -> c_int;
        pub fn Rweb_view_get_error(view: Rid) -> *const c_char;
    }
}

struct App {
    window: Rid,
    switcher: Rid,
    status: Rid,
    status_right: Rid,

    toolbar: Rid,
    toolbar_panel: Rid,
    pause: Rid,

    sidebar_app: Rid,
    sidebar_panel: Rid,
    sidebar_help: Rid,

    help_page: Rid,
    help_view: Rid,
    help_back: Rid,
    help_forward: Rid,
    help_reload: Rid,
    help_status: Rid,
    help_loaded: bool,
    help_status_text: String,

    app_item: Rid,
    panel_item: Rid,

    pane: i32,
    config: Config,
    schema: Schema,
}

static mut APP: *mut App = std::ptr::null_mut();

fn app() -> &'static mut App {
    unsafe { &mut *APP }
}

pub(crate) fn config_mut() -> &'static mut Config {
    &mut app().config
}

pub(crate) fn schema_ref() -> &'static Schema {
    &app().schema
}

/* ---------------------------------------------------------------------------
 *  Shell state
 * ------------------------------------------------------------------------- */

pub(crate) fn set_status(text: &str) {
    unsafe { Rlabel_set_text(app().status, cstr(text).as_ptr()) };
}

fn update_right_status() {
    let a = app();
    let p = panel::panel();
    let text = if a.pane == PANE_APPLICATION {
        format!("Raven Workbench {}   {}", VERSION, a.config.channel)
    } else {
        format!(
            "P-101 {:5.1} bar   F-201 {:5.1} L/min   {}{}",
            p.pressure(),
            p.flow(),
            if p.running() { "running" } else { "paused" },
            if p.alarm() { "   ALARM" } else { "" }
        )
    };
    unsafe { Rlabel_set_text(a.status_right, cstr(&text).as_ptr()) };
}

fn sync_shell() {
    let a = app();
    unsafe {
        Rmenu_item_set_checked(a.app_item, if a.pane == PANE_APPLICATION { 1 } else { 0 });
        Rmenu_item_set_checked(a.panel_item, if a.pane == PANE_PANEL { 1 } else { 0 });
        Rbutton_set_style(
            a.sidebar_app,
            if a.pane == PANE_APPLICATION { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT },
        );
        Rbutton_set_style(
            a.sidebar_panel,
            if a.pane == PANE_PANEL { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT },
        );
        Rbutton_set_style(
            a.sidebar_help,
            if a.pane == PANE_HELP { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT },
        );
        if a.toolbar_panel != R_INVALID_ID {
            Rbutton_set_style(
                a.toolbar_panel,
                if a.pane == PANE_PANEL { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT },
            );
        }
        if a.pause != R_INVALID_ID {
            Rbutton_set_text(
                a.pause,
                cstr(if panel::panel().running() { "Pause" } else { "Resume" }).as_ptr(),
            );
        }
    }
    update_right_status();
}

fn help_file(page: &str) -> String {
    format!("{}/{}", HELP_DIR, page)
}

/// The embedded browser starts with the first help page, not at startup.
fn ensure_help() {
    let a = app();
    if a.help_view != R_INVALID_ID {
        return;
    }
    a.help_view = unsafe { web::Rweb_view_create() };
    unsafe {
        Rwidget_add_child(a.help_page, a.help_view);
        Rslot_set_fill(a.help_view, 1.0);
    }
}

fn load_help(page: &str) {
    ensure_help();
    let a = app();
    // A file URL carrying the theme in use; spaces are the only characters
    // escaped. The guide then styles itself from ?theme=dark or ?theme=light.
    let path = help_file(page);
    let url = format!(
        "file://{}{}",
        path.replace(' ', "%20"),
        if unsafe { Rtheme_is_dark() } != 0 { "?theme=dark" } else { "?theme=light" }
    );
    unsafe { web::Rweb_view_load_url(a.help_view, cstr(&url).as_ptr()) };
    a.help_loaded = true;
}

fn update_help_status() {
    let a = app();
    if a.help_view == R_INVALID_ID {
        return;
    }
    let text = unsafe {
        let error = text(web::Rweb_view_get_error(a.help_view));
        if !error.is_empty() {
            "Documentation unavailable".to_string()
        } else if web::Rweb_view_is_loading(a.help_view) != 0 {
            "Loading...".to_string()
        } else {
            let title = text(web::Rweb_view_get_title(a.help_view));
            if title.is_empty() { "User guide".to_string() } else { title }
        }
    };
    if a.help_status_text != text {
        a.help_status_text = text.clone();
        unsafe { Rlabel_set_text(a.help_status, cstr(&text).as_ptr()) };
        unsafe { Rwindow_request_redraw(a.window) };
    }
    unsafe {
        Rwidget_set_enabled(a.help_back, if web::Rweb_view_can_go_back(a.help_view) != 0 { 1 } else { 0 });
        Rwidget_set_enabled(
            a.help_forward,
            if web::Rweb_view_can_go_forward(a.help_view) != 0 { 1 } else { 0 },
        );
    }
}

fn set_pane(pane: i32) {
    let pane = if matches!(pane, PANE_PANEL | PANE_HELP | PANE_PREFERENCES) { pane } else { PANE_APPLICATION };
    let a = app();
    a.pane = pane;
    if a.pane == PANE_HELP {
        ensure_help();
        if !a.help_loaded {
            load_help("index.html");
        }
    }
    unsafe { Rswitcher_set_index(a.switcher, a.pane) };
    sync_shell();
    match a.pane {
        PANE_APPLICATION => set_status("Application information."),
        PANE_PANEL => set_status("Instrument panel opened."),
        PANE_HELP => set_status("User guide."),
        _ => set_status("Preferences."),
    }
}

fn toggle_panel() {
    set_pane(if app().pane == PANE_PANEL { PANE_APPLICATION } else { PANE_PANEL });
}

/* ---------------------------------------------------------------------------
 *  Actions
 * ------------------------------------------------------------------------- */

fn handle_action(sender: Rid, action: &str) {
    let a = app();
    match action {
        "view.application" => set_pane(PANE_APPLICATION),
        "view.panel" => {
            // The checkable item toggled itself before firing: read it.
            let checked = unsafe { Rmenu_item_is_checked(sender) } != 0;
            set_pane(if checked { PANE_PANEL } else { PANE_APPLICATION });
        }
        "plant.pause" => {
            panel::panel().set_running(!panel::panel().running());
            sync_shell();
            set_status(if panel::panel().running() { "Simulation running." } else { "Simulation paused." });
        }
        "plant.reset" => {
            panel::panel().reset();
            panel::panel().set_running(true);
            sync_shell();
            set_status("Plant reset to a cold start.");
        }
        "edit.preferences" => set_pane(PANE_PREFERENCES),
        "clipboard.version" => {
            let text = format!("Raven Workbench {} ({})", VERSION, a.config.channel);
            unsafe { Rclipboard_set_text(cstr(&text).as_ptr()) };
            set_status("Copied to the clipboard.");
        }
        "clipboard.config" => {
            let path = a.config.path.clone();
            unsafe { Rclipboard_set_text(cstr(&path).as_ptr()) };
            set_status("Copied to the clipboard.");
        }
        "clipboard.readings" => {
            let p = panel::panel();
            let text = format!("P-101 {:.1} bar   F-201 {:.1} L/min", p.pressure(), p.flow());
            unsafe { Rclipboard_set_text(cstr(&text).as_ptr()) };
            set_status("Copied to the clipboard.");
        }
        "preferences.save" => {
            preferences::save();
        }
        "preferences.reset" => preferences::reset(),
        "help.reload" => {
            if a.help_view != R_INVALID_ID {
                unsafe { web::Rweb_view_reload(a.help_view) };
            }
        }
        "help.back" => {
            if a.help_view != R_INVALID_ID {
                unsafe { web::Rweb_view_go_back(a.help_view) };
            }
        }
        "help.forward" => {
            if a.help_view != R_INVALID_ID {
                unsafe { web::Rweb_view_go_forward(a.help_view) };
            }
        }
        "help.guide" => {
            set_pane(PANE_HELP);
            load_help("index.html");
        }
        "help.config" => {
            set_pane(PANE_HELP);
            load_help("configuration.html");
        }
        "help.about" => {
            let message = format!(
                "Raven Workbench {} ({})\n\nRaven {}.{}.{}\n\nConfiguration:\n  {}\n  {}",
                VERSION,
                a.config.channel,
                RAVEN_VERSION_MAJOR,
                RAVEN_VERSION_MINOR,
                RAVEN_VERSION_PATCH,
                a.config.path,
                a.config.status()
            );
            unsafe {
                Rmessage_box(a.window, cstr("About Workbench").as_ptr(), cstr(&message).as_ptr(), R_MESSAGE_OK);
            }
            set_status("About closed.");
        }
        "theme.dark" => {
            a.config.theme = "dark".into();
            apply_live_settings();
            set_status("Dark theme. Saved with Preferences.");
        }
        "theme.light" => {
            a.config.theme = "light".into();
            apply_live_settings();
            set_status("Light theme. Saved with Preferences.");
        }
        _ => {}
    }
}

/* ---------------------------------------------------------------------------
 *  Frame
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn on_frame(delta: f64, _data: *mut c_void) {
    let a = app();
    // The help pane updates its own title from the browser.
    if a.pane == PANE_HELP {
        update_help_status();
        return;
    }
    // The application page is static; only the panel costs frames.
    if a.pane != PANE_PANEL {
        return;
    }

    panel::panel().simulate(delta);
    panel::panel().refresh();
    update_right_status();
    if panel::panel().alarm() {
        set_status("HIGH PRESSURE - the interlock is armed.");
    }
    Rwindow_request_redraw(a.window);
}

/* ---------------------------------------------------------------------------
 *  UI
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn on_sidebar_application(_button: Rid, _data: *mut c_void) {
    set_pane(PANE_APPLICATION);
}

unsafe extern "C" fn on_sidebar_panel(_button: Rid, _data: *mut c_void) {
    set_pane(PANE_PANEL);
}

unsafe extern "C" fn on_sidebar_help(_button: Rid, _data: *mut c_void) {
    set_pane(PANE_HELP);
}

unsafe extern "C" fn on_alarms(_button: Rid, _data: *mut c_void) {
    set_status("Alarms is a placeholder: the panel's alarm lamp is the real one.");
}

unsafe extern "C" fn on_toggle_button(_button: Rid, _data: *mut c_void) {
    toggle_panel();
}

unsafe extern "C" fn on_pause_button(_button: Rid, _data: *mut c_void) {
    panel::panel().set_running(!panel::panel().running());
    sync_shell();
    set_status(if panel::panel().running() { "Simulation running." } else { "Simulation paused." });
}

unsafe extern "C" fn on_reset_button(_button: Rid, _data: *mut c_void) {
    panel::panel().reset();
    panel::panel().set_running(true);
    sync_shell();
    set_status("Plant reset to a cold start.");
}

unsafe extern "C" fn on_help_back(_button: Rid, _data: *mut c_void) {
    if app().help_view != R_INVALID_ID {
        web::Rweb_view_go_back(app().help_view);
    }
}

unsafe extern "C" fn on_help_forward(_button: Rid, _data: *mut c_void) {
    if app().help_view != R_INVALID_ID {
        web::Rweb_view_go_forward(app().help_view);
    }
}

unsafe extern "C" fn on_help_reload(_button: Rid, _data: *mut c_void) {
    if app().help_view != R_INVALID_ID {
        web::Rweb_view_reload(app().help_view);
    }
}

fn info_row(parent: Rid, key: &str, value: &str, name: Option<&str>) -> Rid {
    unsafe {
        let row = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 12.0);

        let label = Rwidget_add_child(row, Rlabel_create(cstr(key).as_ptr()));
        Rlabel_set_text_size(label, 12.0);
        Rwidget_set_opacity(label, 0.55);
        Rwidget_set_min_size(label, 110.0, 0.0);

        let text_widget = Rwidget_add_child(row, Rlabel_create(cstr(value).as_ptr()));
        Rlabel_set_text_size(text_widget, 12.0);
        Rslot_set_fill(text_widget, 1.0);
        if let Some(name) = name {
            Rwidget_set_name(text_widget, cstr(name).as_ptr());
        }
        text_widget
    }
}

fn build_application_page() -> Rid {
    let a = app();
    unsafe {
        let page = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(page, 8.0);
        Rslot_set_padding(page, 36.0, 32.0, 36.0, 28.0);

        let title = Rwidget_add_child(page, Rlabel_create(cstr("Raven Workbench").as_ptr()));
        Rlabel_set_text_size(title, 30.0);

        let subtitle = Rwidget_add_child(
            page,
            Rlabel_create(cstr(&format!("Release {}   ·   channel {}", VERSION, a.config.channel)).as_ptr()),
        );
        Rlabel_set_text_size(subtitle, 13.0);
        Rwidget_set_opacity(subtitle, 0.65);

        Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

        info_row(page, "Application", "Raven Workbench", None);
        info_row(page, "Version", &format!("{} ({} build)", VERSION, "Cargo"), Some("info.version"));
        info_row(page, "Channel", &a.config.channel, Some("info.channel"));
        info_row(
            page,
            "Raven",
            &format!("{}.{}.{}", RAVEN_VERSION_MAJOR, RAVEN_VERSION_MINOR, RAVEN_VERSION_PATCH),
            None,
        );
        info_row(page, "Configuration", &a.config.path, Some("info.config"));
        info_row(page, "Config state", &a.config.status(), Some("info.state"));

        Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

        let shortcuts = Rwidget_add_child(page, Rlabel_create(cstr("Panes and shortcuts").as_ptr()));
        Rlabel_set_text_size(shortcuts, 11.0);
        Rwidget_set_opacity(shortcuts, 0.5);
        info_row(page, "Ctrl+I", "Instrument panel", None);
        info_row(page, "Ctrl+0", "Application", None);
        info_row(page, "Ctrl+Q", "Quit", None);

        let spacer = Rwidget_add_child(page, Rwidget_create(R_WIDGET_SPACER));
        Rslot_set_fill(spacer, 1.0);

        let footer = Rwidget_add_child(
            page,
            Rlabel_create(cstr("Rust implementation · part of the Raven Workbench repository").as_ptr()),
        );
        Rlabel_set_text_size(footer, 10.0);
        Rwidget_set_opacity(footer, 0.35);

        page
    }
}

fn tool_button(bar: Rid, text: &str, tooltip: &str, callback: RButtonClickedCallback) -> Rid {
    unsafe {
        let button = Rwidget_add_child(bar, Rbutton_create(cstr(text).as_ptr()));
        Rbutton_set_clicked_callback(button, callback, std::ptr::null_mut());
        Rwidget_set_tooltip(button, cstr(tooltip).as_ptr());
        button
    }
}

fn sidebar_button(column: Rid, text: &str, tooltip: &str, callback: RButtonClickedCallback) -> Rid {
    unsafe {
        let button = tool_button(column, text, tooltip, callback);
        // No Slot.Fill in a VBox: that would be a share of the *height*.
        Rslot_set_halign(button, R_HALIGN_FILL);
        button
    }
}

/// What the Preferences pane changes that can be applied without a restart.
pub(crate) fn apply_live_settings() {
    let a = app();
    unsafe { Rtheme_apply(if a.config.theme == "light" { R_THEME_LIGHT } else { R_THEME_DARK }) };
    let p = panel::panel();
    p.set_alarm_limit(a.config.alarm_limit);
    p.set_window(a.config.chart_seconds);
    p.set_switch(panel::PANEL_PUMP, a.config.pump);
    p.set_switch(panel::PANEL_VALVE, a.config.valve);
    p.set_switch(panel::PANEL_INTERLOCKS, a.config.armed);
    update_right_status();
    unsafe { Rwindow_request_redraw(a.window) };
}

fn build_help_page() -> Rid {
    unsafe {
        let page = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(page, 0.0);

        let bar = Rwidget_add_child(page, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(bar, 6.0);
        Rslot_set_padding(bar, 8.0, 6.0, 8.0, 6.0);

        app().help_back = tool_button(bar, "Back", "Previous page", Some(on_help_back));
        app().help_forward = tool_button(bar, "Forward", "Next page", Some(on_help_forward));
        app().help_reload = tool_button(bar, "Reload", "Reload the page", Some(on_help_reload));
        Rwidget_set_enabled(app().help_back, 0);
        Rwidget_set_enabled(app().help_forward, 0);

        let spacer = Rwidget_add_child(bar, Rwidget_create(R_WIDGET_SPACER));
        Rslot_set_fill(spacer, 1.0);

        app().help_status = Rwidget_add_child(bar, Rlabel_create(cstr("User guide").as_ptr()));
        Rlabel_set_text_size(app().help_status, 11.0);
        Rwidget_set_opacity(app().help_status, 0.6);
        Rslot_set_valign(app().help_status, R_VALIGN_CENTER);

        Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

        // The WebView is added here the first time the pane is opened.
        app().help_page = Rwidget_add_child(page, Rwidget_create(R_WIDGET_VBOX));
        Rslot_set_fill(app().help_page, 1.0);
        page
    }
}

fn context_menu(labels: &[&str], actions: &[Option<&str>]) -> Rid {
    unsafe {
        let menu = Rwidget_create(R_WIDGET_MENU);
        for (label, action) in labels.iter().zip(actions.iter()) {
            if label.is_empty() {
                Rmenu_add_separator(menu);
                continue;
            }
            let item = Rmenu_add_item(menu, cstr(label).as_ptr());
            if let Some(action) = action {
                Rwidget_set_action(item, cstr(action).as_ptr());
            }
        }
        menu
    }
}

/// One menu per area: Raven walks up from the widget under the pointer, so a
/// menu set on a pane is the menu of everything inside it.
fn attach_context_menus(sidebar: Rid, application_page: Rid, panel_page: Rid, preferences_page: Rid, help_page: Rid) {
    unsafe {
        Rwidget_set_context_menu(
            sidebar,
            context_menu(
                &["Application", "Instrument panel", "User guide", "", "Preferences..."],
                &[
                    Some("view.application"),
                    Some("view.panel"),
                    Some("help.guide"),
                    None,
                    Some("edit.preferences"),
                ],
            ),
        );
        Rwidget_set_context_menu(
            application_page,
            context_menu(
                &["User guide", "Preferences...", "", "Copy version", "Copy configuration path"],
                &[
                    Some("help.guide"),
                    Some("edit.preferences"),
                    None,
                    Some("clipboard.version"),
                    Some("clipboard.config"),
                ],
            ),
        );
        Rwidget_set_context_menu(
            panel_page,
            context_menu(
                &["Pause the simulation", "Reset the plant", "", "Copy the readings"],
                &[Some("plant.pause"), Some("plant.reset"), None, Some("clipboard.readings")],
            ),
        );
        Rwidget_set_context_menu(
            preferences_page,
            context_menu(
                &["Save", "Reset to defaults", "", "Copy configuration path"],
                &[
                    Some("preferences.save"),
                    Some("preferences.reset"),
                    None,
                    Some("clipboard.config"),
                ],
            ),
        );
        Rwidget_set_context_menu(
            help_page,
            context_menu(
                &["Back", "Forward", "Reload", "", "Preferences..."],
                &[
                    Some("help.back"),
                    Some("help.forward"),
                    Some("help.reload"),
                    None,
                    Some("edit.preferences"),
                ],
            ),
        );
    }
}

fn build_window() {
    let a = app();
    // A second window (the test builds two) must not keep the old handles.
    a.window = R_INVALID_ID;
    a.switcher = R_INVALID_ID;
    a.status = R_INVALID_ID;
    a.status_right = R_INVALID_ID;
    a.toolbar = R_INVALID_ID;
    a.toolbar_panel = R_INVALID_ID;
    a.pause = R_INVALID_ID;
    a.sidebar_app = R_INVALID_ID;
    a.sidebar_panel = R_INVALID_ID;
    a.sidebar_help = R_INVALID_ID;
    a.app_item = R_INVALID_ID;
    a.panel_item = R_INVALID_ID;
    a.help_page = R_INVALID_ID;
    a.help_view = R_INVALID_ID;
    a.help_status = R_INVALID_ID;
    a.help_back = R_INVALID_ID;
    a.help_forward = R_INVALID_ID;
    a.help_reload = R_INVALID_ID;
    a.help_loaded = false;
    a.help_status_text.clear();
    preferences::init_prefs();

    unsafe {
        Rtheme_apply(if a.config.theme == "light" { R_THEME_LIGHT } else { R_THEME_DARK });
        a.window = Rwindow_create(cstr("Raven Workbench").as_ptr(), 1280, 820, R_WINDOW_DEFAULT as u32);

        let root = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(root, 0.0);
        Rwindow_set_content(a.window, root);

        // --- the menu bar --------------------------------------------------
        let bar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_MENU_BAR));

        let file = Rmenu_add_menu(bar, cstr("File").as_ptr());
        let quit = Rmenu_add_item(file, cstr("Quit").as_ptr());
        Rmenu_item_set_shortcut(quit, R_KEY_Q, R_MOD_CTRL as u32);
        Rwidget_set_action(quit, cstr("app.quit").as_ptr());

        let edit = Rmenu_add_menu(bar, cstr("Edit").as_ptr());
        let preferences_item = Rmenu_add_item(edit, cstr("Preferences...").as_ptr());
        Rmenu_item_set_shortcut(preferences_item, R_KEY_COMMA, R_MOD_CTRL as u32);
        Rwidget_set_action(preferences_item, cstr("edit.preferences").as_ptr());

        let view = Rmenu_add_menu(bar, cstr("View").as_ptr());
        a.app_item = Rmenu_add_item(view, cstr("Application").as_ptr());
        Rmenu_item_set_checkable(a.app_item, 1);
        Rmenu_item_set_shortcut(a.app_item, R_KEY_0, R_MOD_CTRL as u32);
        Rwidget_set_action(a.app_item, cstr("view.application").as_ptr());

        a.panel_item = Rmenu_add_item(view, cstr("Instrument panel").as_ptr());
        Rmenu_item_set_checkable(a.panel_item, 1);
        Rmenu_item_set_shortcut(a.panel_item, R_KEY_I, R_MOD_CTRL as u32);
        Rwidget_set_action(a.panel_item, cstr("view.panel").as_ptr());

        Rmenu_add_separator(view);
        let pause_item = Rmenu_add_item(view, cstr("Pause the simulation").as_ptr());
        Rwidget_set_action(pause_item, cstr("plant.pause").as_ptr());
        let reset_item = Rmenu_add_item(view, cstr("Reset the plant").as_ptr());
        Rwidget_set_action(reset_item, cstr("plant.reset").as_ptr());
        Rmenu_add_separator(view);
        let dark = Rmenu_add_item(view, cstr("Dark").as_ptr());
        Rwidget_set_action(dark, cstr("theme.dark").as_ptr());
        let light = Rmenu_add_item(view, cstr("Light").as_ptr());
        Rwidget_set_action(light, cstr("theme.light").as_ptr());

        let help = Rmenu_add_menu(bar, cstr("Help").as_ptr());
        let guide = Rmenu_add_item(help, cstr("User guide").as_ptr());
        Rmenu_item_set_shortcut(guide, R_KEY_F1, 0);
        Rwidget_set_action(guide, cstr("help.guide").as_ptr());
        Rmenu_add_separator(help);
        let about = Rmenu_add_item(help, cstr("About Workbench...").as_ptr());
        Rwidget_set_action(about, cstr("help.about").as_ptr());

        // --- the toolbar, when the configuration wants one ------------------
        if a.config.toolbar {
            a.toolbar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(a.toolbar, 6.0);
            Rslot_set_padding(a.toolbar, 8.0, 6.0, 8.0, 6.0);

            a.toolbar_panel = tool_button(
                a.toolbar,
                "Instrument panel",
                "Open the instrument panel  (Ctrl+I)",
                Some(on_toggle_button),
            );
            a.pause = tool_button(a.toolbar, "Pause", "Pause or resume the simulated plant", Some(on_pause_button));
            tool_button(a.toolbar, "Reset", "Reset the plant and the trend", Some(on_reset_button));

            let spacer = Rwidget_add_child(a.toolbar, Rwidget_create(R_WIDGET_SPACER));
            Rslot_set_fill(spacer, 1.0);

            let plant = Rwidget_add_child(a.toolbar, Rlabel_create(cstr("SIMULATED PLANT").as_ptr()));
            Rlabel_set_text_size(plant, 11.0);
            Rwidget_set_opacity(plant, 0.55);
            Rslot_set_valign(plant, R_VALIGN_CENTER);
        } else {
            // The layout still needs a pause widget: the status bar has one.
            a.pause = R_INVALID_ID;
        }

        // --- the body -------------------------------------------------------
        let body = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(body, 0.0);
        Rslot_set_fill(body, 1.0);

        let sidebar = Rwidget_add_child(body, Rwidget_create(R_WIDGET_VBOX));
        Rpanel_set_spacing(sidebar, 6.0);
        Rslot_set_padding(sidebar, 12.0, 12.0, 12.0, 12.0);
        Rwidget_set_min_size(sidebar, 190.0, 0.0);

        let panes = Rwidget_add_child(sidebar, Rlabel_create(cstr("PANES").as_ptr()));
        Rlabel_set_text_size(panes, 10.0);
        Rwidget_set_opacity(panes, 0.6);

        a.sidebar_app = sidebar_button(
            sidebar,
            "Application",
            "Application information  (Ctrl+0)",
            Some(on_sidebar_application),
        );
        a.sidebar_panel = sidebar_button(
            sidebar,
            "Instrument panel",
            "Open the instrument panel  (Ctrl+I)",
            Some(on_sidebar_panel),
        );
        a.sidebar_help = sidebar_button(sidebar, "User guide", "This documentation  (F1)", Some(on_sidebar_help));
        sidebar_button(sidebar, "Alarms", "A placeholder pane", Some(on_alarms));

        let sidebar_spacer = Rwidget_add_child(sidebar, Rwidget_create(R_WIDGET_SPACER));
        Rslot_set_fill(sidebar_spacer, 1.0);

        let version = Rwidget_add_child(sidebar, Rlabel_create(cstr(&format!("Raven Workbench\n{}", VERSION)).as_ptr()));
        Rlabel_set_text_size(version, 10.0);
        Rwidget_set_opacity(version, 0.45);

        let divider = Rwidget_add_child(body, Rwidget_create(R_WIDGET_SEPARATOR));
        Rseparator_set_orientation(divider, R_VERTICAL);

        a.switcher = Rwidget_add_child(body, Rwidget_create(R_WIDGET_SWITCHER));
        Rslot_set_fill(a.switcher, 1.0);
        let application_page = build_application_page();
        Rwidget_add_child(a.switcher, application_page);

        let pane = panel::panel().build();
        Rslot_set_padding(pane, 16.0, 16.0, 16.0, 16.0);
        Rwidget_add_child(a.switcher, pane);

        let help_page = build_help_page();
        Rwidget_add_child(a.switcher, help_page);

        let preferences_page = preferences::build();
        Rslot_set_padding(preferences_page, 16.0, 16.0, 16.0, 16.0);
        Rwidget_add_child(a.switcher, preferences_page);

        // A panel is transparent to the pointer by default; these areas show a
        // menu, so they must be hit. Children still take the clicks first.
        Rwidget_set_hit_testable(sidebar, 1);
        Rwidget_set_hit_testable(application_page, 1);
        Rwidget_set_hit_testable(pane, 1);
        Rwidget_set_hit_testable(preferences_page, 1);
        Rwidget_set_hit_testable(help_page, 1);

        attach_context_menus(sidebar, application_page, pane, preferences_page, help_page);

        // --- the status bar -------------------------------------------------
        Rwidget_add_child(root, Rwidget_create(R_WIDGET_SEPARATOR));
        let status_bar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(status_bar, 12.0);
        Rslot_set_padding(status_bar, 12.0, 5.0, 12.0, 5.0);

        a.status = Rwidget_add_child(status_bar, Rlabel_create(cstr(&a.config.status()).as_ptr()));
        Rlabel_set_text_size(a.status, 11.0);
        Rslot_set_fill(a.status, 1.0);
        Rslot_set_valign(a.status, R_VALIGN_CENTER);

        a.status_right = Rwidget_add_child(status_bar, Rlabel_create(cstr("").as_ptr()));
        Rlabel_set_text_size(a.status_right, 11.0);
        Rwidget_set_opacity(a.status_right, 0.7);
        Rslot_set_valign(a.status_right, R_VALIGN_CENTER);

        // --- actions --------------------------------------------------------
        for name in [
            "view.application",
            "view.panel",
            "plant.pause",
            "plant.reset",
            "edit.preferences",
            "clipboard.version",
            "clipboard.config",
            "clipboard.readings",
            "preferences.save",
            "preferences.reset",
            "help.reload",
            "help.back",
            "help.forward",
            "help.guide",
            "help.config",
            "help.about",
            "theme.dark",
            "theme.light",
        ] {
            action_bind(name, handle_action);
        }

        // --- apply the configuration ----------------------------------------
        let p = panel::panel();
        p.set_alarm_limit(a.config.alarm_limit);
        p.set_window(a.config.chart_seconds);
        p.set_switch(panel::PANEL_PUMP, a.config.pump);
        p.set_switch(panel::PANEL_VALVE, a.config.valve);
        p.set_switch(panel::PANEL_INTERLOCKS, a.config.armed);

        set_pane(a.config.pane);
        // the first message says where the settings came from
        set_status(&a.config.status());
        update_right_status();
    }
}

/* ---------------------------------------------------------------------------
 *  Start
 * ------------------------------------------------------------------------- */

fn init(headless: bool) -> bool {
    unsafe {
        let mut desc: RInitDesc = std::mem::zeroed();
        desc.backend = if headless { R_BACKEND_NULL } else { R_BACKEND_VULKAN };
        desc.headless = headless as c_int;
        desc.app_name = cstr("Raven workbench").into_raw();
        if Rinit_ex(&desc) == 0 {
            eprintln!("cannot start Raven (is a Vulkan driver installed?)");
            return false;
        }
    }
    true
}

/// Moves the pointer to the middle of a widget and clicks it.
fn click(widget: Rid) {
    if widget == R_INVALID_ID {
        return;
    }
    let (mut x, mut y, mut width, mut height) = (0.0f32, 0.0f32, 0.0f32, 0.0f32);
    unsafe {
        Rwidget_get_rect(widget, &mut x, &mut y, &mut width, &mut height);
        Rwindow_inject_mouse_move(app().window, x + width * 0.5, y + height * 0.5);
        Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 1);
        Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 0);
    }
}

fn save_ppm(window: Rid, path: &str) -> bool {
    let (mut width, mut height) = (0i32, 0i32);
    let pixels = unsafe { Rwindow_capture(window, &mut width, &mut height) };
    if pixels.is_null() {
        return false;
    }
    let mut out = format!("P6\n{} {}\n255\n", width, height).into_bytes();
    let count = (width * height) as usize;
    for i in 0..count {
        out.extend_from_slice(unsafe { std::slice::from_ref(&*pixels.add(i * 4)) });
        out.extend_from_slice(unsafe { std::slice::from_ref(&*pixels.add(i * 4 + 1)) });
        out.extend_from_slice(unsafe { std::slice::from_ref(&*pixels.add(i * 4 + 2)) });
    }
    unsafe { Rpixels_free(pixels) };
    std::fs::write(path, out).is_ok()
}

fn run_tests() -> i32 {
    let mut failures = 0;
    macro_rules! check {
        ($condition:expr, $message:expr) => {
            if !$condition {
                println!("  FAIL: {}", $message);
                failures += 1;
            }
        };
    }

    // --- the JSONC reader ---------------------------------------------------
    let config_path = "/tmp/raven_workbench_rust.jsonc";
    std::fs::write(
        config_path,
        "{\n  // a hand-written configuration\n  \"release\": { \"channel\": \"beta\" },\n  \"ui\": { \"theme\": \"light\", \"toolbar\": false, },\n  /* the pane to open at startup */\n  \"startup\": { \"pane\": \"instrument-panel\", \"pump\": true, \"valve\": true },\n  \"simulation\": { \"alarm_limit\": 50, \"chart_seconds\": 5 },\n}\n",
    )
    .unwrap();
    let custom = config::load(config_path);
    check!(custom.loaded, "the JSONC file loads (comments and trailing commas)");
    check!(custom.channel == "beta", "release.channel");
    check!(custom.theme == "light", "ui.theme");
    check!(!custom.toolbar, "ui.toolbar");
    check!(custom.pane == 1, "startup.pane");
    check!(custom.pump && custom.valve && !custom.armed, "startup switches");
    check!(custom.alarm_limit == 50.0, "simulation.alarm_limit");
    check!(custom.chart_seconds == 5.0, "simulation.chart_seconds");
    check!(custom.settings == 8, "eight settings applied");

    std::fs::write(config_path, "{ \"ui\": { \"theme\": } }").unwrap();
    let broken = config::load(config_path);
    check!(!broken.loaded && !broken.error.is_empty(), "a syntax error is reported");
    check!(broken.channel == "dev", "a broken file keeps the defaults");
    check!(config::load("/tmp/raven_workbench_absent.jsonc").error.is_empty(), "a missing file is not an error");

    // --- the default application --------------------------------------------
    app().config = config::load(DEFAULT_CONFIG);
    build_window();
    unsafe { Rdraw_frame() };

    check!(app().pane == PANE_APPLICATION, "the application pane is the default");
    check!(unsafe { Rswitcher_get_index(app().switcher) } == PANE_APPLICATION, "the switcher shows it");
    check!(app().toolbar != R_INVALID_ID, "the toolbar is shown by default");
    check!(panel::panel().window_seconds == 10.0, "the chart window defaults to 10 s");
    check!(panel::panel().alarm_limit == 75.0, "the alarm defaults to 75 bar");

    {
        let version = widget_find(app().window, "info.version");
        let channel = widget_find(app().window, "info.channel");
        let config_label = widget_find(app().window, "info.config");
        check!(
            version != R_INVALID_ID && unsafe { text(Rlabel_get_text(version)) }.contains(VERSION),
            "the application pane shows the version"
        );
        check!(
            channel != R_INVALID_ID && unsafe { text(Rlabel_get_text(channel)) } == "dev",
            "the application pane shows the channel"
        );
        check!(
            config_label != R_INVALID_ID && unsafe { text(Rlabel_get_text(config_label)) } == DEFAULT_CONFIG,
            "the application pane shows the configuration path"
        );
    }

    // the toolbar opens the panel, the side bar goes back
    click(app().toolbar_panel);
    check!(app().pane == PANE_PANEL, "the toolbar opens the instrument panel");
    check!(unsafe { Rswitcher_get_index(app().switcher) } == PANE_PANEL, "the switcher follows");
    check!(unsafe { Rmenu_item_is_checked(app().panel_item) } != 0, "the View item follows the toolbar");
    click(app().sidebar_app);
    check!(app().pane == PANE_APPLICATION, "the side bar goes back to the application");

    // the menu does the same, through the action the item fires
    unsafe {
        Rmenu_item_set_checked(app().panel_item, 1);
        Raction_fire(cstr("view.panel").as_ptr(), app().panel_item);
    }
    check!(app().pane == PANE_PANEL, "View > Instrument panel opens it");
    unsafe {
        Rmenu_item_set_checked(app().app_item, 1);
        Raction_fire(cstr("view.application").as_ptr(), app().app_item);
    }
    check!(app().pane == PANE_APPLICATION, "View > Application goes back");

    // the panel runs, and a hidden pane costs nothing
    set_pane(PANE_PANEL);
    unsafe { Rdraw_frame() };
    check!(
        panel::panel().paints(panel::PAINT_METER) > 0
            && panel::panel().paints(panel::PAINT_DIGITAL) > 0
            && panel::panel().paints(panel::PAINT_CHART) > 0
            && panel::panel().paints(panel::PAINT_LED) > 0,
        "every custom widget was painted"
    );
    for _ in 0..90 {
        panel::panel().simulate(1.0 / 60.0);
    }
    let samples = panel::panel().samples();
    check!(samples > 0, "the panel collected samples");
    set_pane(PANE_APPLICATION);
    unsafe { on_frame(1.0, std::ptr::null_mut()) };
    check!(panel::panel().samples() == samples, "a hidden pane does not simulate");

    // pause and reset from the toolbar
    set_pane(PANE_PANEL);
    let was_running = panel::panel().running();
    click(app().pause);
    check!(panel::panel().running() != was_running, "the toolbar Pause toggles the plant");
    click(app().pause);
    unsafe { Raction_fire(cstr("plant.reset").as_ptr(), R_INVALID_ID) };
    check!(panel::panel().samples() == 0, "Reset empties the trend");
    check!(
        unsafe { text(Rlabel_get_text(app().status)) } == "Plant reset to a cold start.",
        "the status bar names the last command"
    );

    // --- the documentation pane ---------------------------------------------
    unsafe { Raction_fire(cstr("help.guide").as_ptr(), R_INVALID_ID) };
    check!(app().pane == PANE_HELP, "Help > User guide opens the documentation pane");
    check!(unsafe { Rswitcher_get_index(app().switcher) } == PANE_HELP, "the switcher shows it");
    check!(app().help_view != R_INVALID_ID, "the embedded browser view exists");
    check!(unsafe { web::Rweb_is_available() } == 0, "the test runs without a browser");
    {
        let error = unsafe { text(web::Rweb_view_get_error(app().help_view)) };
        check!(!error.is_empty(), "the view explains that the browser is missing");
    }
    unsafe { Raction_fire(cstr("help.config").as_ptr(), R_INVALID_ID) };
    check!(app().pane == PANE_HELP, "Help > Configuration stays in the pane");
    click(app().sidebar_app);
    check!(app().pane == PANE_APPLICATION, "the side bar returns to the application");

    // --- the preferences editor ---------------------------------------------
    check!(app().schema.loaded, "the schema loads");
    check!(app().schema.groups.len() == 4, "four groups in the schema");
    check!(app().schema.setting_count() == 9, "nine settings in the schema");

    unsafe { Raction_fire(cstr("edit.preferences").as_ptr(), R_INVALID_ID) };
    check!(app().pane == PANE_PREFERENCES, "Edit > Preferences opens the pane");
    check!(unsafe { Rswitcher_get_index(app().switcher) } == PANE_PREFERENCES, "the switcher shows it");
    check!(preferences::shown() == 9, "every setting has a row");

    preferences::set_filter("trend");
    check!(preferences::shown() == 1, "the search filters the rows");
    preferences::set_filter("");
    check!(preferences::shown() == 9, "clearing the search shows them all");

    {
        let toolbar = widget_find(app().window, "setting.ui.toolbar");
        check!(toolbar != R_INVALID_ID, "the toolbar setting has a control");
        let before = app().config.toolbar;
        click(toolbar);
        check!(app().config.toolbar != before, "clicking the control changes the setting");
    }

    {
        // a hand-edited file keeps its comments when the settings change
        let path = "/tmp/raven_workbench_rust_comments.jsonc";
        std::fs::write(
            path,
            "{\n  // keep this comment\n  \"release\": { \"channel\": \"dev\" },\n  \"ui\": {\n    \"theme\": \"dark\", // inline comment\n    \"toolbar\": true\n  },\n  \"startup\": { \"pane\": \"application\", \"pump\": false, \"valve\": false },\n  \"simulation\": { \"alarm_limit\": 75, \"chart_seconds\": 10 }\n}\n",
        )
        .unwrap();

        let mut edited = config::load(path);
        check!(edited.loaded, "a commented file loads");
        edited.theme = "light".into();
        edited.armed = true; // missing from the file: it must be added
        check!(config::save(path, &edited).is_ok(), "save edits the file in place");
        let text = std::fs::read_to_string(path).unwrap();
        check!(text.contains("// keep this comment"), "comments survive a save");
        check!(text.contains("// inline comment"), "an inline comment survives a save");
        check!(text.contains("\"theme\": \"light\""), "the value was replaced");
        check!(text.contains("\"armed\": true"), "a missing key was added");
        let reread = config::load(path);
        check!(reread.theme == "light" && reread.armed, "the edited file reads back");
        let _ = std::fs::remove_file(path);
    }

    {
        // save where the test may write, then read the file back
        app().config.channel = "stable".into();
        app().config.path = "/tmp/raven_workbench_rust_saved.jsonc".into();
        preferences::init_prefs();
        check!(preferences::save(), "Save writes the configuration");
        let reloaded = config::load(&app().config.path);
        check!(reloaded.loaded, "the written file loads");
        check!(reloaded.channel == "stable", "the change reached the file");
        preferences::reset();
        check!(app().config.channel == "dev", "Reset restores the defaults");
        let _ = std::fs::remove_file("/tmp/raven_workbench_rust_saved.jsonc");
    }

    click(app().sidebar_app);
    check!(app().pane == PANE_APPLICATION, "the side bar leaves the preferences");

    // --- context menus ------------------------------------------------------
    {
        set_pane(PANE_PANEL);
        unsafe { Rdraw_frame() };
        let row = unsafe { Rtheme_get_metric(R_METRIC_ROW_HEIGHT) };

        // the side bar has a menu; its first item goes back to the application
        unsafe {
            Rwindow_inject_mouse_move(app().window, 60.0, 420.0);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 1);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 0);
        }
        check!(unsafe { Rwindow_has_popup(app().window) } != 0, "right-clicking the side bar opens a menu");
        unsafe {
            Rwindow_inject_mouse_move(app().window, 90.0, 420.0 + 4.0 + row * 0.5);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 1);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 0);
        }
        check!(app().pane == PANE_APPLICATION, "the side bar menu opens the application pane");
        check!(unsafe { Rwindow_has_popup(app().window) } == 0, "the menu closes after the choice");

        // Escape closes a menu
        unsafe {
            Rwindow_inject_mouse_move(app().window, 60.0, 420.0);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 1);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 0);
        }
        check!(unsafe { Rwindow_has_popup(app().window) } != 0, "the menu opens again");
        unsafe {
            Rwindow_inject_key(app().window, R_KEY_ESCAPE, 1, 0);
            Rwindow_inject_key(app().window, R_KEY_ESCAPE, 0, 0);
        }
        check!(unsafe { Rwindow_has_popup(app().window) } == 0, "Escape closes it");

        // the instrument panel has a menu too: its first item pauses the plant
        set_pane(PANE_PANEL);
        unsafe { Rdraw_frame() };
        let was_running = panel::panel().running();
        unsafe {
            Rwindow_inject_mouse_move(app().window, 600.0, 500.0);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 1);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 0);
        }
        check!(unsafe { Rwindow_has_popup(app().window) } != 0, "right-clicking the panel opens a menu");
        unsafe {
            Rwindow_inject_mouse_move(app().window, 630.0, 500.0 + 4.0 + row * 0.5);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 1);
            Rwindow_inject_mouse_button(app().window, R_MOUSE_LEFT, 0);
        }
        check!(panel::panel().running() != was_running, "the panel menu pauses the simulation");
        panel::panel().set_running(true); // leave it running for the rest of the test
    }

    // the clipboard actions behind the menus
    unsafe { Raction_fire(cstr("clipboard.version").as_ptr(), R_INVALID_ID) };
    {
        let copied = unsafe { text(Rclipboard_get_text()) };
        check!(copied.contains(VERSION), "Copy version puts the version on the clipboard");
    }

    // --- a configured application -------------------------------------------
    unsafe { Rwindow_destroy(app().window) };
    app().config = custom.clone();
    build_window();
    unsafe { Rdraw_frame() };

    check!(app().toolbar == R_INVALID_ID, "ui.toolbar = false hides the toolbar");
    check!(app().pane == PANE_PANEL, "startup.pane opens the instrument panel");
    check!(unsafe { Rswitcher_get_index(app().switcher) } == PANE_PANEL, "the switcher shows the panel");
    check!(panel::panel().alarm_limit == 50.0, "simulation.alarm_limit reaches the panel");
    check!(panel::panel().window_seconds == 5.0, "simulation.chart_seconds reaches the chart");
    check!(
        panel::panel().switch(panel::PANEL_PUMP) && panel::panel().switch(panel::PANEL_VALVE),
        "startup switches are on"
    );
    check!(unsafe { Rtheme_is_dark() } == 0, "ui.theme = light");

    {
        let channel = widget_find(app().window, "info.channel");
        let state = widget_find(app().window, "info.state");
        check!(
            channel != R_INVALID_ID && unsafe { text(Rlabel_get_text(channel)) } == "beta",
            "the application pane shows the configured channel"
        );
        check!(
            state != R_INVALID_ID && unsafe { text(Rlabel_get_text(state)) }.contains("8 settings"),
            "the application pane reports the loaded settings"
        );
    }

    // the pump from the configuration raises the pressure by itself
    let start_pressure = panel::panel().pressure();
    for _ in 0..180 {
        panel::panel().simulate(1.0 / 60.0);
    }
    check!(panel::panel().pressure() > start_pressure + 20.0, "the configured pump runs the plant");

    println!(
        "workbench (rust): {} checks failed, {} samples, config {}",
        failures,
        panel::panel().samples(),
        custom.status()
    );
    unsafe { Rshutdown() };
    println!("{}", if failures == 0 { "PASS" } else { "FAIL" });
    if failures == 0 { 0 } else { 1 }
}

fn config_path(arguments: &[String]) -> String {
    if let Some(index) = arguments.iter().position(|a| a == "--config") {
        if let Some(path) = arguments.get(index + 1) {
            return path.clone();
        }
    }
    if let Ok(path) = std::env::var("WORKBENCH_CONFIG") {
        return path;
    }
    DEFAULT_CONFIG.to_string()
}

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    let test = arguments.iter().any(|a| a == "--test");
    let capture = arguments
        .iter()
        .position(|a| a == "--capture")
        .and_then(|index| arguments.get(index + 1))
        .cloned();
    let pane_override = arguments
        .iter()
        .position(|a| a == "--pane")
        .and_then(|index| arguments.get(index + 1))
        .cloned();
    let show_menu = arguments.iter().any(|a| a == "--menu");
    let frames = arguments
        .iter()
        .position(|a| a == "--frames")
        .and_then(|index| arguments.get(index + 1))
        .and_then(|value| value.parse().ok())
        .unwrap_or(120);

    if !init(test) {
        std::process::exit(1);
    }
    // The test does not start a browser; the view reports it, the pane still works.
    if test {
        unsafe { web::Rweb_set_browser_path(cstr("/nonexistent-browser").as_ptr()) };
    }
    panel::init_panel();
    unsafe {
        APP = Box::into_raw(Box::new(App {
            window: R_INVALID_ID,
            switcher: R_INVALID_ID,
            status: R_INVALID_ID,
            status_right: R_INVALID_ID,
            toolbar: R_INVALID_ID,
            toolbar_panel: R_INVALID_ID,
            pause: R_INVALID_ID,
            sidebar_app: R_INVALID_ID,
            sidebar_panel: R_INVALID_ID,
            sidebar_help: R_INVALID_ID,
            help_page: R_INVALID_ID,
            help_view: R_INVALID_ID,
            help_back: R_INVALID_ID,
            help_forward: R_INVALID_ID,
            help_reload: R_INVALID_ID,
            help_status: R_INVALID_ID,
            help_loaded: false,
            help_status_text: String::new(),
            app_item: R_INVALID_ID,
            panel_item: R_INVALID_ID,
            pane: PANE_APPLICATION,
            config: config::defaults(DEFAULT_CONFIG),
            schema: schema::Schema::default(),
        }));
    }
    app().schema = schema::load(SCHEMA_PATH);
    app().config = config::load(&config_path(&arguments));
    if let Some(pane) = pane_override {
        app().config.pane = match pane.as_str() {
            "instrument-panel" => PANE_PANEL,
            "user-guide" => PANE_HELP,
            "preferences" => PANE_PREFERENCES,
            _ => PANE_APPLICATION,
        };
    }

    let code = if test {
        run_tests()
    } else {
        build_window();
        if let Some(path) = capture {
            unsafe {
                // keep the real pointer off the menus, and show the plant running
                Rwindow_inject_mouse_move(app().window, 4.0, 700.0);
                Rdraw_frame();
            }
            if app().pane == PANE_PANEL {
                if !panel::panel().switch(panel::PANEL_PUMP) {
                    click(panel::panel().button(panel::PANEL_PUMP));
                }
                if !panel::panel().switch(panel::PANEL_VALVE) {
                    click(panel::panel().button(panel::PANEL_VALVE));
                }
                if !panel::panel().switch(panel::PANEL_INTERLOCKS) {
                    click(panel::panel().button(panel::PANEL_INTERLOCKS));
                }
            }
            for _ in 0..frames {
                unsafe {
                    on_frame(1.0 / 60.0, std::ptr::null_mut());
                    Rwindow_request_redraw(app().window);
                    Rwait_frame(0.01);
                }
            }
            if show_menu {
                let (x, y) = if app().pane == PANE_PANEL {
                    (620.0f32, 470.0f32)
                } else {
                    (60.0f32, 420.0f32)
                };
                unsafe {
                    Rwindow_inject_mouse_move(app().window, x, y);
                    Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 1);
                    Rwindow_inject_mouse_button(app().window, R_MOUSE_RIGHT, 0);
                }
                for _ in 0..5 {
                    unsafe {
                        Rwindow_request_redraw(app().window);
                        Rwait_frame(0.02);
                    }
                }
            }
            let saved = save_ppm(app().window, &path);
            unsafe { Rshutdown() };
            if !saved {
                eprintln!("cannot write {}", path);
                1
            } else {
                println!("saved {}", path);
                0
            }
        } else {
            unsafe {
                Rset_frame_callback(Some(on_frame), std::ptr::null_mut());
                while Rwait_frame(-1.0) != 0 {}
                Rshutdown();
            }
            0
        }
    };

    panel::free_panel();
    preferences::free_prefs();
    unsafe {
        drop(Box::from_raw(APP));
        APP = std::ptr::null_mut();
    }
    std::process::exit(code);
}
