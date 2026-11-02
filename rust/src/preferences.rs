//! The Preferences pane: one row per setting of the schema.
//!
//! The pane is built from workbench.schema.json. Each row shows the title and
//! description of the setting and a control of the right type: a check box for
//! a boolean, a combo box for an enum, a spin box for a number with a range, a
//! text field otherwise. Changes are written into the Config and applied at
//! once; the file is only written by `save`.

use crate::{config, config_mut, set_status, schema_ref};
use raven::*;
use std::ffi::c_void;
use std::os::raw::c_int;

pub struct Binding {
    path: String,
    title: String,
    choices: Vec<String>,
}

pub struct Preferences {
    root: Rid,
    search: Rid,
    list: Rid,
    scroll: Rid,
    meta: Rid,
    status: Rid,

    filter: String,
    bindings: Vec<Box<Binding>>,
    shown: i32,
}

static mut PREFS: *mut Preferences = std::ptr::null_mut();

pub fn prefs() -> &'static mut Preferences {
    unsafe { &mut *PREFS }
}

pub fn init_prefs() {
    unsafe {
        if PREFS.is_null() {
            PREFS = Box::into_raw(Box::new(Preferences {
                root: R_INVALID_ID,
                search: R_INVALID_ID,
                list: R_INVALID_ID,
                scroll: R_INVALID_ID,
                meta: R_INVALID_ID,
                status: R_INVALID_ID,
                filter: String::new(),
                bindings: Vec::new(),
                shown: 0,
            }));
        } else {
            prefs().bindings.clear();
            prefs().filter.clear();
            prefs().shown = 0;
        }
    }
}

pub fn free_prefs() {
    unsafe {
        if !PREFS.is_null() {
            drop(Box::from_raw(PREFS));
            PREFS = std::ptr::null_mut();
        }
    }
}

fn lowered(text: &str) -> String {
    text.to_lowercase()
}

fn report(text: &str) {
    set_status(text);
}

fn changed(binding: &Binding) {
    crate::apply_live_settings();
    report(&format!("Changed \"{}\" - not saved yet.", binding.title));
}

/* ---------------------------------------------------------------------------
 *  Controls
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn on_check(_widget: Rid, checked: c_int, data: *mut c_void) {
    let binding = &*(data as *const Binding);
    config::set(config_mut(), &binding.path, if checked != 0 { "true" } else { "false" });
    changed(binding);
}

unsafe extern "C" fn on_choice(_widget: Rid, index: c_int, data: *mut c_void) {
    let binding = &*(data as *const Binding);
    if index < 0 || index as usize >= binding.choices.len() {
        return;
    }
    config::set(config_mut(), &binding.path, &binding.choices[index as usize]);
    changed(binding);
}

unsafe extern "C" fn on_number(_widget: Rid, value: f64, data: *mut c_void) {
    let binding = &*(data as *const Binding);
    config::set(config_mut(), &binding.path, &format!("{}", value));
    changed(binding);
}

unsafe extern "C" fn on_text(_widget: Rid, text: *const std::os::raw::c_char, data: *mut c_void) {
    let binding = &*(data as *const Binding);
    let value = raven::text(text);
    config::set(config_mut(), &binding.path, &value);
    changed(binding);
}

fn make_control(setting: &crate::schema::Setting, binding: *mut Binding) -> Rid {
    let value = match config::get(config_mut(), &setting.path) {
        Some(value) => value,
        None => {
            // The schema names a setting this build does not use.
            let note = unsafe { Rlabel_create(cstr("not used by this build").as_ptr()) };
            unsafe {
                Rlabel_set_text_size(note, 11.0);
                Rwidget_set_opacity(note, 0.5);
            }
            return note;
        }
    };

    let control = unsafe {
        if setting.kind == "boolean" {
            let control = Rcheckbox_create(cstr("").as_ptr());
            Rcheckbox_set_checked(control, if value == "true" { 1 } else { 0 });
            Rcheckbox_set_changed_callback(control, Some(on_check), binding as *mut c_void);
            control
        } else if !setting.choices.is_empty() {
            let control = Rwidget_create(R_WIDGET_COMBO_BOX);
            for choice in &setting.choices {
                Rcombo_box_add_item(control, cstr(choice).as_ptr());
            }
            let mut selected = 0;
            for (index, choice) in setting.choices.iter().enumerate() {
                if *choice == value {
                    selected = index as c_int;
                }
            }
            Rcombo_box_set_selected(control, selected);
            Rcombo_box_set_changed_callback(control, Some(on_choice), binding as *mut c_void);
            control
        } else if setting.kind == "number" || setting.kind == "integer" {
            let integer = setting.kind == "integer";
            let control = Rwidget_create(R_WIDGET_SPIN_BOX);
            Rspin_box_set_range(
                control,
                if setting.has_range { setting.minimum } else { 0.0 },
                if setting.has_range { setting.maximum } else { 100.0 },
            );
            Rspin_box_set_step(control, if integer { 1.0 } else { 0.5 });
            Rspin_box_set_decimals(control, if integer { 0 } else { 1 });
            Rspin_box_set_value(control, value.parse().unwrap_or(0.0));
            Rspin_box_set_changed_callback(control, Some(on_number), binding as *mut c_void);
            control
        } else {
            let control = Rwidget_create(R_WIDGET_TEXT_INPUT);
            Rtext_input_set_text(control, cstr(&value).as_ptr());
            Rtext_input_set_changed_callback(control, Some(on_text), binding as *mut c_void);
            control
        }
    };

    unsafe {
        // The text column takes the slack; the control keeps its size at the right.
        if setting.kind != "boolean" {
            Rwidget_set_min_size(control, 220.0, 0.0);
        }
        Rslot_set_halign(control, R_HALIGN_RIGHT);
        Rslot_set_valign(control, R_VALIGN_CENTER);
    }
    control
}

/* ---------------------------------------------------------------------------
 *  Rows and the list
 * ------------------------------------------------------------------------- */

fn add_group_label(title: &str) {
    unsafe {
        let label = Rwidget_add_child(prefs().list, Rlabel_create(cstr(title).as_ptr()));
        Rlabel_set_text_size(label, 12.0);
        Rwidget_set_opacity(label, 0.6);
        Rslot_set_padding(label, 0.0, 14.0, 0.0, 4.0);
    }
}

fn add_row(setting: &crate::schema::Setting) {
    unsafe {
        let row = Rwidget_add_child(prefs().list, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 16.0);
        Rslot_set_padding(row, 0.0, 6.0, 0.0, 6.0);

        let text = Rwidget_add_child(row, Rwidget_create(R_WIDGET_VBOX));
        Rpanel_set_spacing(text, 2.0);
        Rslot_set_fill(text, 1.0);

        let title = Rwidget_add_child(text, Rlabel_create(cstr(&setting.title).as_ptr()));
        Rlabel_set_text_size(title, 13.0);

        if !setting.description.is_empty() {
            let description = Rwidget_add_child(text, Rlabel_create(cstr(&setting.description).as_ptr()));
            Rlabel_set_text_size(description, 11.0);
            Rwidget_set_opacity(description, 0.55);
            Rlabel_set_wrap(description, 1);
        }

        prefs().bindings.push(Box::new(Binding {
            path: setting.path.clone(),
            title: setting.title.clone(),
            choices: setting.choices.clone(),
        }));
        let binding = &mut **prefs().bindings.last_mut().unwrap() as *mut Binding;

        let control = make_control(setting, binding);
        Rwidget_add_child(row, control);
        Rwidget_set_name(control, cstr(&format!("setting.{}", setting.path)).as_ptr());
    }
}

pub fn rebuild() {
    let preferences = prefs();
    unsafe {
        if preferences.list != R_INVALID_ID {
            Rwidget_destroy(preferences.list);
            preferences.list = R_INVALID_ID;
        }
        preferences.bindings.clear();
        preferences.shown = 0;

        preferences.list = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(preferences.list, 2.0);
        Rwidget_add_child(preferences.scroll, preferences.list);
        Rslot_set_fill(preferences.list, 1.0);
    }

    let needle = lowered(&preferences.filter);
    let schema = schema_ref();
    for group in &schema.groups {
        let mut header = false;
        for setting in &group.settings {
            let keep = needle.is_empty()
                || lowered(&setting.title).contains(&needle)
                || lowered(&setting.description).contains(&needle)
                || lowered(&setting.path).contains(&needle);
            if !keep {
                continue;
            }
            if !header {
                add_group_label(&group.title);
                header = true;
            }
            add_row(setting);
            prefs().shown += 1;
        }
    }

    let config = config_mut();
    let meta = if schema.loaded {
        format!("config  {}\nschema  {}", config.path, schema.path)
    } else {
        format!("config  {}\nschema  {}  ({})", config.path, schema.path, schema.error)
    };
    unsafe {
        Rlabel_set_text(prefs().meta, cstr(&meta).as_ptr());
        Rlabel_set_text(
            prefs().status,
            cstr(&format!("Showing {} of {} settings", prefs().shown, schema.setting_count())).as_ptr(),
        );
    }
}

/* ---------------------------------------------------------------------------
 *  Buttons
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn on_search(_widget: Rid, text: *const std::os::raw::c_char, _data: *mut c_void) {
    prefs().filter = raven::text(text);
    rebuild();
}

unsafe extern "C" fn on_reset(_button: Rid, _data: *mut c_void) {
    reset();
}

unsafe extern "C" fn on_save(_button: Rid, _data: *mut c_void) {
    save();
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

pub fn build() -> Rid {
    let preferences = prefs();
    preferences.bindings.clear();
    preferences.shown = 0;

    let root = unsafe { Rwidget_create(R_WIDGET_VBOX) };
    preferences.root = root;
    unsafe {
        Rpanel_set_spacing(root, 10.0);
        Rslot_set_padding(root, 24.0, 20.0, 24.0, 16.0);

        let header = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(header, 12.0);

        let title = Rwidget_add_child(header, Rlabel_create(cstr("Preferences").as_ptr()));
        Rlabel_set_text_size(title, 20.0);
        Rslot_set_valign(title, R_VALIGN_CENTER);

        let spacer = Rwidget_add_child(header, Rwidget_create(R_WIDGET_SPACER));
        Rslot_set_fill(spacer, 1.0);

        preferences.search = Rwidget_add_child(header, Rwidget_create(R_WIDGET_TEXT_INPUT));
        Rtext_input_set_placeholder(preferences.search, cstr("Search settings").as_ptr());
        Rwidget_set_min_size(preferences.search, 260.0, 0.0);
        Rslot_set_valign(preferences.search, R_VALIGN_CENTER);
        Rtext_input_set_changed_callback(preferences.search, Some(on_search), std::ptr::null_mut());

        preferences.meta = Rwidget_add_child(root, Rlabel_create(cstr("").as_ptr()));
        Rlabel_set_text_size(preferences.meta, 11.0);
        Rwidget_set_opacity(preferences.meta, 0.55);

        Rwidget_add_child(root, Rwidget_create(R_WIDGET_SEPARATOR));

        preferences.scroll = Rwidget_add_child(root, Rwidget_create(R_WIDGET_SCROLL_BOX));
        Rslot_set_fill(preferences.scroll, 1.0);

        Rwidget_add_child(root, Rwidget_create(R_WIDGET_SEPARATOR));

        let footer = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(footer, 8.0);

        preferences.status = Rwidget_add_child(footer, Rlabel_create(cstr("").as_ptr()));
        Rlabel_set_text_size(preferences.status, 11.0);
        Rwidget_set_opacity(preferences.status, 0.6);
        Rslot_set_fill(preferences.status, 1.0);
        Rslot_set_valign(preferences.status, R_VALIGN_CENTER);

        let reset_button = Rwidget_add_child(footer, Rbutton_create(cstr("Reset to defaults").as_ptr()));
        Rbutton_set_clicked_callback(reset_button, Some(on_reset), std::ptr::null_mut());

        let save_button = Rwidget_add_child(footer, Rbutton_create(cstr("Save").as_ptr()));
        Rbutton_set_style(save_button, R_BUTTON_PRIMARY);
        Rbutton_set_clicked_callback(save_button, Some(on_save), std::ptr::null_mut());
    }

    rebuild();
    root
}

/// Writes the configuration file; false (and a message) when it cannot.
pub fn save() -> bool {
    let config = config_mut();
    match config::save(&config.path, config) {
        Ok(()) => {
            config.loaded = true;
            config.error.clear();
            config.settings = schema_ref().setting_count() as i32;
            report(&format!("Saved {}. Toolbar and first pane apply at the next start.", config.path));
            true
        }
        Err(error) => {
            report(&error);
            false
        }
    }
}

/// Back to the defaults of the application (not written to the file yet).
pub fn reset() {
    let path = config_mut().path.clone();
    *config_mut() = config::defaults(&path);
    crate::apply_live_settings();
    rebuild();
    report("Defaults restored in memory - Save writes them.");
}

/// Filters the rows: a substring of the title, the description or the key.
pub fn set_filter(text: &str) {
    prefs().filter = text.to_string();
    unsafe { Rtext_input_set_text(prefs().search, cstr(text).as_ptr()) };
    rebuild();
}

/// Rows matching the current filter (tests).
pub fn shown() -> i32 {
    prefs().shown
}
