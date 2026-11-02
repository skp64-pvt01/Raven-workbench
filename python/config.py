"""Application configuration, read from a JSONC file (JSON with comments).

The workbench keeps its own small reader: no dependency, and the file is meant
to be edited by hand, so comments and trailing commas are accepted.

    // workbench.jsonc
    {
        "ui": { "theme": "light", "toolbar": true },
        "startup": { "pane": "application", "pump": true },
        "simulation": { "alarm_limit": 75, "chart_seconds": 10 }
    }

Every field is optional: a missing file, a missing key or a wrong type keeps the
default, and the loader reports what it did in `status`.
"""

import jsonc


class Config:
    def __init__(self, path):
        # what happened
        self.path = path
        self.loaded = False
        self.error = ""
        self.settings = 0

        # the settings
        self.channel = "dev"
        self.theme = "dark"
        self.toolbar = True
        self.pane = 0
        self.pump = False
        self.valve = False
        self.armed = False
        self.alarm_limit = 75.0
        self.chart_seconds = 10.0

    def status(self):
        """One line for the status bar and the application pane."""
        if not self.loaded and not self.error:
            return "not found, defaults"
        if self.error:
            return "%s, defaults" % self.error
        return "%d setting%s loaded" % (self.settings, "" if self.settings == 1 else "s")

    def copy(self):
        other = Config(self.path)
        other.__dict__.update(self.__dict__)
        return other


def _kind_name(kind):
    return {
        jsonc.Json.NULL: "null",
        jsonc.Json.BOOL: "a boolean",
        jsonc.Json.NUMBER: "a number",
        jsonc.Json.STRING: "a string",
        jsonc.Json.ARRAY: "an array",
        jsonc.Json.OBJECT: "an object",
    }.get(kind, "a value")


def _apply(root, config):
    applied = 0
    release = root.member("release")
    if release:
        channel = release.member("channel")
        if channel and channel.kind == jsonc.Json.STRING:
            config.channel = channel.text
            applied += 1

    ui = root.member("ui")
    if ui:
        theme = ui.member("theme")
        if theme and theme.kind == jsonc.Json.STRING and theme.text in ("dark", "light"):
            config.theme = theme.text
            applied += 1
        toolbar = ui.member("toolbar")
        if toolbar and toolbar.kind == jsonc.Json.BOOL:
            config.toolbar = toolbar.boolean
            applied += 1

    startup = root.member("startup")
    if startup:
        pane = startup.member("pane")
        if pane and pane.kind == jsonc.Json.STRING:
            if pane.text == "instrument-panel":
                config.pane = 1
            elif pane.text == "application":
                config.pane = 0
            applied += 1
        for key in ("pump", "valve", "armed"):
            value = startup.member(key)
            if value and value.kind == jsonc.Json.BOOL:
                setattr(config, key, value.boolean)
                applied += 1

    simulation = root.member("simulation")
    if simulation:
        limit = simulation.member("alarm_limit")
        if limit and limit.kind == jsonc.Json.NUMBER:
            config.alarm_limit = min(100.0, max(10.0, limit.number))
            applied += 1
        seconds = simulation.member("chart_seconds")
        if seconds and seconds.kind == jsonc.Json.NUMBER:
            config.chart_seconds = min(10.0, max(1.0, seconds.number))
            applied += 1
    return applied


def defaults(path):
    """The defaults, with `path` recorded and no error."""
    return Config(path)


def load(path):
    """Reads a JSONC file. On failure returns the defaults with `error` set."""
    config = Config(path)
    try:
        with open(path, encoding="utf-8") as file:
            text = file.read()
    except OSError:
        return config  # not found: defaults, no error

    try:
        root = jsonc.parse(text)
    except ValueError as error:
        config.error = str(error)
        return config
    if root.kind != jsonc.Json.OBJECT:
        config.error = "the root is %s, expected an object" % _kind_name(root.kind)
        return config

    config.settings = _apply(root, config)
    config.loaded = True
    return config


def _number_text(value):
    text = "%g" % value
    return text


def get(config, path):
    """Reads one setting by path ("ui.theme"). None when this build does not use it."""
    if path == "release.channel":
        return config.channel
    if path == "ui.theme":
        return config.theme
    if path == "ui.toolbar":
        return "true" if config.toolbar else "false"
    if path == "startup.pane":
        return "instrument-panel" if config.pane == 1 else "application"
    if path == "startup.pump":
        return "true" if config.pump else "false"
    if path == "startup.valve":
        return "true" if config.valve else "false"
    if path == "startup.armed":
        return "true" if config.armed else "false"
    if path == "simulation.alarm_limit":
        return _number_text(config.alarm_limit)
    if path == "simulation.chart_seconds":
        return _number_text(config.chart_seconds)
    return None


def set_value(config, path, value):
    """Writes one setting from its text form ("true", "dark", "75"). Clamps numbers."""
    if path == "release.channel":
        config.channel = value
    elif path == "ui.theme":
        if value not in ("dark", "light"):
            return False
        config.theme = value
    elif path == "ui.toolbar":
        config.toolbar = value == "true"
    elif path == "startup.pane":
        if value not in ("application", "instrument-panel"):
            return False
        config.pane = 1 if value == "instrument-panel" else 0
    elif path == "startup.pump":
        config.pump = value == "true"
    elif path == "startup.valve":
        config.valve = value == "true"
    elif path == "startup.armed":
        config.armed = value == "true"
    elif path == "simulation.alarm_limit":
        try:
            number = float(value)
        except ValueError:
            number = config.alarm_limit
        config.alarm_limit = min(100.0, max(10.0, number))
    elif path == "simulation.chart_seconds":
        try:
            number = float(value)
        except ValueError:
            number = config.chart_seconds
        config.chart_seconds = min(10.0, max(1.0, number))
    else:
        return False
    return True


def _escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def _is_string_path(path):
    return path in ("release.channel", "ui.theme", "startup.pane")


def _value_text(config, path):
    value = get(config, path)
    if value is None:
        return None
    if _is_string_path(path):
        return '"%s"' % _escape(value)
    return value


PATHS = (
    "release.channel", "ui.theme", "ui.toolbar", "startup.pane", "startup.pump",
    "startup.valve", "startup.armed", "simulation.alarm_limit", "simulation.chart_seconds",
)


def _save_in_place(path, text, root, config):
    edits = []  # (start, end, replacement)
    pending = []  # [group, [member strings]]

    for setting_path in PATHS:
        replacement = _value_text(config, setting_path)
        if replacement is None:
            continue
        group_name, leaf = setting_path.split(".", 1)
        group = root.member(group_name)
        if group is None or group.kind != jsonc.Json.OBJECT:
            return False, 'the file has no "%s" object; fix it before saving' % group_name
        member = group.member(leaf)
        if member is not None:
            if text[member.start:member.end] != replacement:
                edits.append((member.start, member.end, replacement))
            continue
        entry = next((item for item in pending if item[0] == group_name), None)
        if entry is None:
            entry = [group_name, []]
            pending.append(entry)
        entry[1].append('"%s": %s' % (leaf, replacement))

    for group_name, members in pending:
        group = root.member(group_name)
        close = group.end - 1  # the closing brace
        comma = bool(group.members) and "," in text[group.members[-1][1].end:close]
        snippet = ""
        for index, member in enumerate(members):
            if index == 0:
                snippet += "\n\t\t" if (not group.members or comma) else ",\n\t\t"
            else:
                snippet += ",\n\t\t"
            snippet += member
        if not group.members:
            snippet += "\n\t"
        edits.append((close, close, snippet))

    # Largest offset first: replacing a value never moves an earlier one.
    for start, end, replacement in sorted(edits, key=lambda edit: edit[0], reverse=True):
        text = text[:start] + replacement + text[end:]

    # A safety net: never write a file the reader would reject.
    try:
        jsonc.parse(text)
    except ValueError as error:
        return False, "the change would make the file invalid (%s); nothing was written" % error

    try:
        with open(path, "w", encoding="utf-8") as file:
            file.write(text)
    except OSError:
        return False, "cannot write %s" % path
    return True, ""


def _write_new(path, config):
    text = "// Raven Workbench - settings written by the Preferences pane.\n"
    text += "// JSON with comments. workbench.schema.json describes every key.\n"
    text += "{\n"
    text += '\t"release": { "channel": "%s" },\n\n' % _escape(config.channel)
    text += '\t"ui": {\n'
    text += '\t\t"theme": "%s",\n' % _escape(config.theme)
    text += '\t\t"toolbar": %s\n' % ("true" if config.toolbar else "false")
    text += "\t},\n\n"
    text += '\t"startup": {\n'
    text += '\t\t"pane": "%s",\n' % ("instrument-panel" if config.pane == 1 else "application")
    text += '\t\t"pump": %s,\n' % ("true" if config.pump else "false")
    text += '\t\t"valve": %s,\n' % ("true" if config.valve else "false")
    text += '\t\t"armed": %s\n' % ("true" if config.armed else "false")
    text += "\t},\n\n"
    text += '\t"simulation": {\n'
    text += '\t\t"alarm_limit": %s,\n' % _number_text(config.alarm_limit)
    text += '\t\t"chart_seconds": %s\n' % _number_text(config.chart_seconds)
    text += "\t}\n"
    text += "}\n"
    try:
        with open(path, "w", encoding="utf-8") as file:
            file.write(text)
    except OSError:
        return False, "cannot write %s" % path
    return True, ""


def save(path, config):
    """Writes the whole configuration, with comments. Returns (ok, error)."""
    try:
        with open(path, encoding="utf-8") as file:
            text = file.read()
    except OSError:
        return _write_new(path, config)

    try:
        root = jsonc.parse(text)
    except ValueError as error:
        return False, "the file has a syntax error (%s); it was not overwritten" % error
    if root.kind != jsonc.Json.OBJECT:
        return False, "the file is not a JSON object; it was not overwritten"
    return _save_in_place(path, text, root, config)
