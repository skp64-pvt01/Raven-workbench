"""The settings schema, read from workbench.schema.json.

The Preferences pane is built from it: each group and each setting provides its
title, description, type, range or choices, and default. The reader understands
the subset this application needs - object properties, boolean / string /
number / integer, enum, minimum and maximum - and ignores the rest of JSON
Schema, so the same file stays a valid schema for editors.
"""

import jsonc


class Setting:
    def __init__(self, path):
        self.path = path
        self.title = ""
        self.description = ""
        self.type = "string"
        self.choices = []
        self.minimum = 0.0
        self.maximum = 0.0
        self.has_range = False
        self.default_text = ""


class Group:
    def __init__(self, title):
        self.title = title
        self.settings = []


class Schema:
    def __init__(self, path):
        self.path = path
        self.title = ""
        self.error = ""
        self.loaded = False
        self.groups = []

    def setting_count(self):
        return sum(len(group.settings) for group in self.groups)


def _text_of(obj, key, fallback=""):
    value = obj.member(key)
    return value.text if value is not None and value.kind == jsonc.Json.STRING else fallback


def _read_setting(property_value, path):
    setting = Setting(path)
    setting.title = _text_of(property_value, "title", path.split(".", 1)[-1])
    setting.description = _text_of(property_value, "description")
    setting.type = _text_of(property_value, "type", "string")

    choices = property_value.member("enum")
    if choices is not None and choices.kind == jsonc.Json.ARRAY:
        setting.choices = [item.text for item in choices.items if item.kind == jsonc.Json.STRING]

    minimum = property_value.member("minimum")
    maximum = property_value.member("maximum")
    if (minimum is not None and minimum.kind == jsonc.Json.NUMBER
            and maximum is not None and maximum.kind == jsonc.Json.NUMBER):
        setting.minimum = minimum.number
        setting.maximum = maximum.number
        setting.has_range = True

    fallback = property_value.member("default")
    if fallback is not None:
        if fallback.kind == jsonc.Json.BOOL:
            setting.default_text = "true" if fallback.boolean else "false"
        elif fallback.kind == jsonc.Json.NUMBER:
            setting.default_text = "%g" % fallback.number
        elif fallback.kind == jsonc.Json.STRING:
            setting.default_text = fallback.text
    return setting


def load(path):
    schema = Schema(path)
    try:
        with open(path, encoding="utf-8") as file:
            text = file.read()
    except OSError:
        schema.error = "not found"
        return schema

    try:
        root = jsonc.parse(text)
    except ValueError as error:
        schema.error = str(error)
        return schema
    if root.kind != jsonc.Json.OBJECT:
        schema.error = "the root is not an object"
        return schema
    schema.title = _text_of(root, "title")

    properties = root.member("properties")
    if properties is None or properties.kind != jsonc.Json.OBJECT:
        schema.error = "no properties"
        return schema

    for group_name, group_value in properties.members:
        nested = group_value.member("properties")
        if nested is None or nested.kind != jsonc.Json.OBJECT:
            continue
        group = Group(_text_of(group_value, "title", group_name))
        for setting_name, setting_value in nested.members:
            group.settings.append(_read_setting(setting_value, group_name + "." + setting_name))
        schema.groups.append(group)

    schema.loaded = True
    return schema
