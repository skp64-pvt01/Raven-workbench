//! The settings schema, read from workbench.schema.json.
//!
//! The Preferences pane is built from it: each group and each setting provides
//! its title, description, type, range or choices, and default. The reader
//! understands the subset this application needs - object properties, boolean /
//! string / number / integer, enum, minimum and maximum - and ignores the rest
//! of JSON Schema, so the same file stays a valid schema for editors.

use crate::json;

#[derive(Clone, Debug, Default)]
pub struct Setting {
    pub path: String,
    pub title: String,
    pub description: String,
    pub kind: String,
    pub choices: Vec<String>,
    pub minimum: f64,
    pub maximum: f64,
    pub has_range: bool,
    pub default_text: String,
}

#[derive(Clone, Debug, Default)]
pub struct Group {
    pub title: String,
    pub settings: Vec<Setting>,
}

#[derive(Clone, Debug, Default)]
pub struct Schema {
    pub path: String,
    pub title: String,
    pub error: String,
    pub loaded: bool,
    pub groups: Vec<Group>,
}

impl Schema {
    pub fn setting_count(&self) -> usize {
        self.groups.iter().map(|group| group.settings.len()).sum()
    }
}

fn text_of(object: &json::Json, key: &str, fallback: &str) -> String {
    match object.member(key) {
        Some(value) if value.kind == json::Kind::String => value.text.clone(),
        _ => fallback.to_string(),
    }
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

fn read_setting(property: &json::Json, path: &str) -> Setting {
    let leaf = path.split_once('.').map(|(_, leaf)| leaf).unwrap_or(path);
    let mut setting = Setting {
        path: path.to_string(),
        title: text_of(property, "title", leaf),
        description: text_of(property, "description", ""),
        kind: text_of(property, "type", "string"),
        ..Default::default()
    };
    if let Some(choices) = property.member("enum") {
        if choices.kind == json::Kind::Array {
            setting.choices =
                choices.items.iter().filter(|item| item.kind == json::Kind::String).map(|item| item.text.clone()).collect();
        }
    }
    if let (Some(minimum), Some(maximum)) = (property.member("minimum"), property.member("maximum")) {
        if minimum.kind == json::Kind::Number && maximum.kind == json::Kind::Number {
            setting.minimum = minimum.number;
            setting.maximum = maximum.number;
            setting.has_range = true;
        }
    }
    if let Some(fallback) = property.member("default") {
        setting.default_text = match fallback.kind {
            json::Kind::Bool => if fallback.boolean { "true".into() } else { "false".into() },
            json::Kind::Number => number_text(fallback.number),
            json::Kind::String => fallback.text.clone(),
            _ => String::new(),
        };
    }
    setting
}

pub fn load(path: &str) -> Schema {
    let mut schema = Schema { path: path.to_string(), ..Default::default() };
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(_) => {
            schema.error = "not found".into();
            return schema;
        }
    };
    let root = match json::parse(&text) {
        Ok(root) => root,
        Err(error) => {
            schema.error = error;
            return schema;
        }
    };
    if root.kind != json::Kind::Object {
        schema.error = "the root is not an object".into();
        return schema;
    }
    schema.title = text_of(&root, "title", "");
    let properties = match root.member("properties") {
        Some(properties) if properties.kind == json::Kind::Object => properties,
        _ => {
            schema.error = "no properties".into();
            return schema;
        }
    };
    for (group_name, group_value) in &properties.members {
        let nested = match group_value.member("properties") {
            Some(nested) if nested.kind == json::Kind::Object => nested,
            _ => continue,
        };
        let mut group = Group { title: text_of(group_value, "title", group_name), ..Default::default() };
        for (setting_name, setting_value) in &nested.members {
            let path = format!("{}.{}", group_name, setting_name);
            group.settings.push(read_setting(setting_value, &path));
        }
        schema.groups.push(group);
    }
    schema.loaded = true;
    schema
}
