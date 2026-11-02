//! Application configuration, read from a JSONC file (JSON with comments).
//!
//! The workbench keeps its own small reader: no dependency, and the file is
//! meant to be edited by hand, so comments and trailing commas are accepted.
//!
//!     // workbench.jsonc
//!     {
//!         "ui": { "theme": "light", "toolbar": true },
//!         "startup": { "pane": "application", "pump": true },
//!         "simulation": { "alarm_limit": 75, "chart_seconds": 10 }
//!     }
//!
//! Every field is optional: a missing file, a missing key or a wrong type keeps
//! the default, and the loader reports what it did in `status`.

use crate::json;

#[derive(Clone, Debug)]
pub struct Config {
    // what happened
    pub path: String,
    pub loaded: bool,
    pub error: String,
    pub settings: i32,

    // the settings
    pub channel: String,
    pub theme: String,
    pub toolbar: bool,
    pub pane: i32,
    pub pump: bool,
    pub valve: bool,
    pub armed: bool,
    pub alarm_limit: f64,
    pub chart_seconds: f64,
}

impl Config {
    /// One line for the status bar and the application pane.
    pub fn status(&self) -> String {
        if !self.loaded && self.error.is_empty() {
            return "not found, defaults".into();
        }
        if !self.error.is_empty() {
            return format!("{}, defaults", self.error);
        }
        format!("{} setting{} loaded", self.settings, if self.settings == 1 { "" } else { "s" })
    }
}

/// The defaults, with `path` recorded and no error.
pub fn defaults(path: &str) -> Config {
    Config {
        path: path.to_string(),
        loaded: false,
        error: String::new(),
        settings: 0,
        channel: "dev".into(),
        theme: "dark".into(),
        toolbar: true,
        pane: 0,
        pump: false,
        valve: false,
        armed: false,
        alarm_limit: 75.0,
        chart_seconds: 10.0,
    }
}

fn kind_name(kind: json::Kind) -> &'static str {
    match kind {
        json::Kind::Null => "null",
        json::Kind::Bool => "a boolean",
        json::Kind::Number => "a number",
        json::Kind::String => "a string",
        json::Kind::Array => "an array",
        json::Kind::Object => "an object",
    }
}

fn apply(root: &json::Json, config: &mut Config) -> i32 {
    let mut applied = 0;
    if let Some(release) = root.member("release") {
        if let Some(channel) = release.member("channel") {
            if channel.kind == json::Kind::String {
                config.channel = channel.text.clone();
                applied += 1;
            }
        }
    }
    if let Some(ui) = root.member("ui") {
        if let Some(theme) = ui.member("theme") {
            if theme.kind == json::Kind::String && (theme.text == "dark" || theme.text == "light") {
                config.theme = theme.text.clone();
                applied += 1;
            }
        }
        if let Some(toolbar) = ui.member("toolbar") {
            if toolbar.kind == json::Kind::Bool {
                config.toolbar = toolbar.boolean;
                applied += 1;
            }
        }
    }
    if let Some(startup) = root.member("startup") {
        if let Some(pane) = startup.member("pane") {
            if pane.kind == json::Kind::String {
                if pane.text == "instrument-panel" {
                    config.pane = 1;
                } else if pane.text == "application" {
                    config.pane = 0;
                }
                applied += 1;
            }
        }
        for (key, slot) in [("pump", 0), ("valve", 1), ("armed", 2)] {
            if let Some(value) = startup.member(key) {
                if value.kind == json::Kind::Bool {
                    match slot {
                        0 => config.pump = value.boolean,
                        1 => config.valve = value.boolean,
                        _ => config.armed = value.boolean,
                    }
                    applied += 1;
                }
            }
        }
    }
    if let Some(simulation) = root.member("simulation") {
        if let Some(limit) = simulation.member("alarm_limit") {
            if limit.kind == json::Kind::Number {
                config.alarm_limit = limit.number.clamp(10.0, 100.0);
                applied += 1;
            }
        }
        if let Some(seconds) = simulation.member("chart_seconds") {
            if seconds.kind == json::Kind::Number {
                config.chart_seconds = seconds.number.clamp(1.0, 10.0);
                applied += 1;
            }
        }
    }
    applied
}

/// Reads a JSONC file. On failure returns the defaults with `error` set.
pub fn load(path: &str) -> Config {
    let mut config = defaults(path);
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(_) => return config, // not found: defaults, no error
    };
    let root = match json::parse(&text) {
        Ok(root) => root,
        Err(error) => {
            config.error = error;
            return config;
        }
    };
    if root.kind != json::Kind::Object {
        config.error = format!("the root is {}, expected an object", kind_name(root.kind));
        return config;
    }
    config.settings = apply(&root, &mut config);
    config.loaded = true;
    config
}

fn number_text(value: f64) -> String {
    let mut text = format!("{}", value);
    if text.contains('.') {
        while text.ends_with('0') {
            text.pop();
        }
        if text.ends_with('.') {
            text.pop();
        }
    }
    text
}

/// Reads one setting by path ("ui.theme"). None when this build does not use it.
pub fn get(config: &Config, path: &str) -> Option<String> {
    let value = match path {
        "release.channel" => config.channel.clone(),
        "ui.theme" => config.theme.clone(),
        "ui.toolbar" => bool_text(config.toolbar),
        "startup.pane" => if config.pane == 1 { "instrument-panel" } else { "application" }.into(),
        "startup.pump" => bool_text(config.pump),
        "startup.valve" => bool_text(config.valve),
        "startup.armed" => bool_text(config.armed),
        "simulation.alarm_limit" => number_text(config.alarm_limit),
        "simulation.chart_seconds" => number_text(config.chart_seconds),
        _ => return None,
    };
    Some(value)
}

fn bool_text(value: bool) -> String {
    if value { "true".into() } else { "false".into() }
}

/// Writes one setting from its text form ("true", "dark", "75"). Clamps numbers.
pub fn set(config: &mut Config, path: &str, value: &str) -> bool {
    match path {
        "release.channel" => config.channel = value.to_string(),
        "ui.theme" => {
            if value != "dark" && value != "light" {
                return false;
            }
            config.theme = value.to_string();
        }
        "ui.toolbar" => config.toolbar = value == "true",
        "startup.pane" => {
            if value != "application" && value != "instrument-panel" {
                return false;
            }
            config.pane = if value == "instrument-panel" { 1 } else { 0 };
        }
        "startup.pump" => config.pump = value == "true",
        "startup.valve" => config.valve = value == "true",
        "startup.armed" => config.armed = value == "true",
        "simulation.alarm_limit" => {
            config.alarm_limit = value.parse::<f64>().unwrap_or(config.alarm_limit).clamp(10.0, 100.0);
        }
        "simulation.chart_seconds" => {
            config.chart_seconds =
                value.parse::<f64>().unwrap_or(config.chart_seconds).clamp(1.0, 10.0);
        }
        _ => return false,
    }
    true
}

fn escape(text: &str) -> String {
    let mut out = String::new();
    for c in text.chars() {
        if c == '\\' || c == '"' {
            out.push('\\');
        }
        out.push(c);
    }
    out
}

fn is_string_path(path: &str) -> bool {
    matches!(path, "release.channel" | "ui.theme" | "startup.pane")
}

/// The literal to write for a setting: a quoted JSON string, or true/false/75.
fn value_text(config: &Config, path: &str) -> Option<String> {
    let value = get(config, path)?;
    if is_string_path(path) {
        return Some(format!("\"{}\"", escape(&value)));
    }
    Some(value)
}

const PATHS: [&str; 9] = [
    "release.channel",
    "ui.theme",
    "ui.toolbar",
    "startup.pane",
    "startup.pump",
    "startup.valve",
    "startup.armed",
    "simulation.alarm_limit",
    "simulation.chart_seconds",
];

struct Edit {
    start: usize,
    end: usize,
    text: String,
}

/// Rewrites the values of the settings in the text the user wrote: comments,
/// blank lines and the layout of everything else stay exactly as they are.
/// A key the file does not have is added to its object.
fn save_in_place(path: &str, text: &mut String, root: &json::Json, config: &Config) -> Result<(), String> {
    let mut edits: Vec<Edit> = Vec::new();
    let mut pending: Vec<(String, Vec<String>)> = Vec::new();

    for path_text in PATHS {
        let replacement = match value_text(config, path_text) {
            Some(text) => text,
            None => continue,
        };
        let (group_name, leaf) = path_text.split_once('.').unwrap();
        let group = match root.member(group_name) {
            Some(group) if group.kind == json::Kind::Object => group,
            _ => return Err(format!("the file has no \"{}\" object; fix it before saving", group_name)),
        };
        if let Some(member) = group.member(leaf) {
            let current = &text[member.start..member.end];
            if current != replacement {
                edits.push(Edit { start: member.start, end: member.end, text: replacement });
            }
            continue;
        }
        if let Some((_, members)) = pending.iter_mut().find(|(name, _)| name == group_name) {
            members.push(format!("\"{}\": {}", leaf, replacement));
        } else {
            pending.push((group_name.to_string(), vec![format!("\"{}\": {}", leaf, replacement)]));
        }
    }

    for (group_name, members) in &pending {
        let group = root.member(group_name).unwrap();
        let close = group.end - 1; // the closing brace
        let comma = match group.members.last() {
            Some((_, last)) => text[last.end..close].contains(','),
            None => false,
        };
        let mut snippet = String::new();
        for (i, member) in members.iter().enumerate() {
            if i == 0 {
                snippet.push_str(if group.members.is_empty() || comma { "\n\t\t" } else { ",\n\t\t" });
            } else {
                snippet.push_str(",\n\t\t");
            }
            snippet.push_str(member);
        }
        if group.members.is_empty() {
            snippet.push_str("\n\t");
        }
        edits.push(Edit { start: close, end: close, text: snippet });
    }

    // Largest offset first: replacing a value never moves an earlier one.
    edits.sort_by(|a, b| b.start.cmp(&a.start));
    for edit in &edits {
        text.replace_range(edit.start..edit.end, &edit.text);
    }

    // A safety net: never write a file the reader would reject.
    if let Err(error) = json::parse(text) {
        return Err(format!("the change would make the file invalid ({}); nothing was written", error));
    }
    std::fs::write(path, text).map_err(|_| format!("cannot write {}", path))
}

/// No file yet (or it could not be read): write a fresh one, with comments.
fn write_new(path: &str, config: &Config) -> Result<(), String> {
    let mut text = String::new();
    text.push_str("// Raven Workbench - settings written by the Preferences pane.\n");
    text.push_str("// JSON with comments. workbench.schema.json describes every key.\n");
    text.push_str("{\n");
    text.push_str(&format!("\t\"release\": {{ \"channel\": \"{}\" }},\n\n", escape(&config.channel)));
    text.push_str("\t\"ui\": {\n");
    text.push_str(&format!("\t\t\"theme\": \"{}\",\n", escape(&config.theme)));
    text.push_str(&format!("\t\t\"toolbar\": {}\n", bool_text(config.toolbar)));
    text.push_str("\t},\n\n");
    text.push_str("\t\"startup\": {\n");
    text.push_str(&format!(
        "\t\t\"pane\": \"{}\",\n",
        if config.pane == 1 { "instrument-panel" } else { "application" }
    ));
    text.push_str(&format!("\t\t\"pump\": {},\n", bool_text(config.pump)));
    text.push_str(&format!("\t\t\"valve\": {},\n", bool_text(config.valve)));
    text.push_str(&format!("\t\t\"armed\": {}\n", bool_text(config.armed)));
    text.push_str("\t},\n\n");
    text.push_str("\t\"simulation\": {\n");
    text.push_str(&format!("\t\t\"alarm_limit\": {},\n", number_text(config.alarm_limit)));
    text.push_str(&format!("\t\t\"chart_seconds\": {}\n", number_text(config.chart_seconds)));
    text.push_str("\t}\n");
    text.push_str("}\n");
    std::fs::write(path, text).map_err(|_| format!("cannot write {}", path))
}

/// Writes the whole configuration, with comments.
pub fn save(path: &str, config: &Config) -> Result<(), String> {
    match std::fs::read_to_string(path) {
        Ok(mut text) => {
            let root = match json::parse(&text) {
                Ok(root) => root,
                Err(error) => {
                    return Err(format!("the file has a syntax error ({}); it was not overwritten", error))
                }
            };
            if root.kind != json::Kind::Object {
                return Err("the file is not a JSON object; it was not overwritten".into());
            }
            save_in_place(path, &mut text, &root, config)
        }
        Err(_) => write_new(path, config),
    }
}
