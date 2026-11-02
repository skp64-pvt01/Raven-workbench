"""The Preferences pane: one row per setting of the schema (Python).

The pane is built from workbench.schema.json. Each row shows the title and
description of the setting and a control of the right type: a check box for a
boolean, a combo box for an enum, a spin box for a number with a range, a text
field otherwise. Changes are written into the config and applied at once; the
file is only written by `save`.
"""

import config as settings
import raven as r


class Preferences:
    def __init__(self, get_config, get_schema, on_changed, on_status):
        self.get_config = get_config
        self.get_schema = get_schema
        self.on_changed = on_changed
        self.on_status = on_status

        self.root = r.R_INVALID_ID
        self.search = r.R_INVALID_ID
        self.list = r.R_INVALID_ID
        self.scroll = r.R_INVALID_ID
        self.meta = r.R_INVALID_ID
        self.status = r.R_INVALID_ID

        self.filter = ""
        self.bindings = []
        self.shown = 0

    # -- helpers ----------------------------------------------------------

    def report(self, text):
        self.on_status(text)

    def changed(self, binding):
        self.on_changed()
        self.report('Changed "%s" - not saved yet.' % binding["title"])

    def on_check(self, binding, checked):
        settings.set_value(self.get_config(), binding["path"], "true" if checked else "false")
        self.changed(binding)

    def on_choice(self, binding, index):
        if 0 <= index < len(binding["choices"]):
            settings.set_value(self.get_config(), binding["path"], binding["choices"][index])
            self.changed(binding)

    def on_number(self, binding, value):
        settings.set_value(self.get_config(), binding["path"], "%g" % value)
        self.changed(binding)

    def on_text(self, binding, text):
        settings.set_value(self.get_config(), binding["path"], text or "")
        self.changed(binding)

    def make_control(self, setting, binding):
        value = settings.get(self.get_config(), setting.path)
        if value is None:
            note = r.Rlabel_create("not used by this build")
            r.Rlabel_set_text_size(note, 11)
            r.Rwidget_set_opacity(note, 0.5)
            return note

        if setting.type == "boolean":
            control = r.Rcheckbox_create("")
            r.Rcheckbox_set_checked(control, value == "true")
            r.Rcheckbox_set_changed_callback(
                control, lambda widget, checked, data: self.on_check(binding, checked))
        elif setting.choices:
            control = r.Rwidget_create(r.R_WIDGET_COMBO_BOX)
            for choice in setting.choices:
                r.Rcombo_box_add_item(control, choice)
            selected = setting.choices.index(value) if value in setting.choices else 0
            r.Rcombo_box_set_selected(control, selected)
            r.Rcombo_box_set_changed_callback(
                control, lambda widget, index, data: self.on_choice(binding, index))
        elif setting.type in ("number", "integer"):
            integer = setting.type == "integer"
            control = r.Rwidget_create(r.R_WIDGET_SPIN_BOX)
            r.Rspin_box_set_range(
                control,
                setting.minimum if setting.has_range else 0.0,
                setting.maximum if setting.has_range else 100.0,
            )
            r.Rspin_box_set_step(control, 1.0 if integer else 0.5)
            r.Rspin_box_set_decimals(control, 0 if integer else 1)
            r.Rspin_box_set_value(control, float(value))
            r.Rspin_box_set_changed_callback(
                control, lambda widget, number, data: self.on_number(binding, number))
        else:
            control = r.Rwidget_create(r.R_WIDGET_TEXT_INPUT)
            r.Rtext_input_set_text(control, value)
            r.Rtext_input_set_changed_callback(
                control, lambda widget, text, data: self.on_text(binding, text))

        # The text column takes the slack; the control keeps its size at the right.
        if setting.type != "boolean":
            r.Rwidget_set_min_size(control, 220, 0)
        r.Rslot_set_halign(control, r.R_HALIGN_RIGHT)
        r.Rslot_set_valign(control, r.R_VALIGN_CENTER)
        return control

    # -- rows -------------------------------------------------------------

    def add_group_label(self, title):
        label = r.Rwidget_add_child(self.list, r.Rlabel_create(title))
        r.Rlabel_set_text_size(label, 12)
        r.Rwidget_set_opacity(label, 0.6)
        r.Rslot_set_padding(label, 0, 14, 0, 4)

    def add_row(self, setting):
        row = r.Rwidget_add_child(self.list, r.Rwidget_create(r.R_WIDGET_HBOX))
        r.Rpanel_set_spacing(row, 16)
        r.Rslot_set_padding(row, 0, 6, 0, 6)

        text = r.Rwidget_add_child(row, r.Rwidget_create(r.R_WIDGET_VBOX))
        r.Rpanel_set_spacing(text, 2)
        r.Rslot_set_fill(text, 1)

        title = r.Rwidget_add_child(text, r.Rlabel_create(setting.title))
        r.Rlabel_set_text_size(title, 13)

        if setting.description:
            description = r.Rwidget_add_child(text, r.Rlabel_create(setting.description))
            r.Rlabel_set_text_size(description, 11)
            r.Rwidget_set_opacity(description, 0.55)
            r.Rlabel_set_wrap(description, 1)

        binding = {"path": setting.path, "title": setting.title, "choices": setting.choices}
        self.bindings.append(binding)
        control = self.make_control(setting, binding)
        r.Rwidget_add_child(row, control)
        r.Rwidget_set_name(control, "setting." + setting.path)

    def rebuild(self):
        if self.list != r.R_INVALID_ID:
            r.Rwidget_destroy(self.list)
            self.list = r.R_INVALID_ID
        self.bindings = []
        self.shown = 0

        self.list = r.Rwidget_create(r.R_WIDGET_VBOX)
        r.Rpanel_set_spacing(self.list, 2)
        r.Rwidget_add_child(self.scroll, self.list)
        r.Rslot_set_fill(self.list, 1)

        needle = self.filter.lower()
        schema = self.get_schema()
        for group in schema.groups:
            header = False
            for setting in group.settings:
                keep = (not needle or needle in setting.title.lower()
                        or needle in setting.description.lower()
                        or needle in setting.path.lower())
                if not keep:
                    continue
                if not header:
                    self.add_group_label(group.title)
                    header = True
                self.add_row(setting)
                self.shown += 1

        config = self.get_config()
        if schema.loaded:
            meta = "config  %s\nschema  %s" % (config.path, schema.path)
        else:
            meta = "config  %s\nschema  %s  (%s)" % (config.path, schema.path, schema.error)
        r.Rlabel_set_text(self.meta, meta)
        r.Rlabel_set_text(self.status, "Showing %d of %d settings" % (self.shown, schema.setting_count()))

    # -- buttons ----------------------------------------------------------

    def on_search(self, widget, text, user_data):
        self.filter = text or ""
        self.rebuild()

    def on_reset(self, button, user_data):
        self.reset()

    def on_save(self, button, user_data):
        self.save()

    def build(self):
        self.bindings = []
        self.shown = 0

        self.root = r.Rwidget_create(r.R_WIDGET_VBOX)
        r.Rpanel_set_spacing(self.root, 10)
        r.Rslot_set_padding(self.root, 24, 20, 24, 16)

        header = r.Rwidget_add_child(self.root, r.Rwidget_create(r.R_WIDGET_HBOX))
        r.Rpanel_set_spacing(header, 12)

        title = r.Rwidget_add_child(header, r.Rlabel_create("Preferences"))
        r.Rlabel_set_text_size(title, 20)
        r.Rslot_set_valign(title, r.R_VALIGN_CENTER)

        spacer = r.Rwidget_add_child(header, r.Rwidget_create(r.R_WIDGET_SPACER))
        r.Rslot_set_fill(spacer, 1)

        self.search = r.Rwidget_add_child(header, r.Rwidget_create(r.R_WIDGET_TEXT_INPUT))
        r.Rtext_input_set_placeholder(self.search, "Search settings")
        r.Rwidget_set_min_size(self.search, 260, 0)
        r.Rslot_set_valign(self.search, r.R_VALIGN_CENTER)
        r.Rtext_input_set_changed_callback(self.search, self.on_search)

        self.meta = r.Rwidget_add_child(self.root, r.Rlabel_create(""))
        r.Rlabel_set_text_size(self.meta, 11)
        r.Rwidget_set_opacity(self.meta, 0.55)

        r.Rwidget_add_child(self.root, r.Rwidget_create(r.R_WIDGET_SEPARATOR))

        self.scroll = r.Rwidget_add_child(self.root, r.Rwidget_create(r.R_WIDGET_SCROLL_BOX))
        r.Rslot_set_fill(self.scroll, 1)

        r.Rwidget_add_child(self.root, r.Rwidget_create(r.R_WIDGET_SEPARATOR))

        footer = r.Rwidget_add_child(self.root, r.Rwidget_create(r.R_WIDGET_HBOX))
        r.Rpanel_set_spacing(footer, 8)

        self.status = r.Rwidget_add_child(footer, r.Rlabel_create(""))
        r.Rlabel_set_text_size(self.status, 11)
        r.Rwidget_set_opacity(self.status, 0.6)
        r.Rslot_set_fill(self.status, 1)
        r.Rslot_set_valign(self.status, r.R_VALIGN_CENTER)

        reset_button = r.Rwidget_add_child(footer, r.Rbutton_create("Reset to defaults"))
        r.Rbutton_set_clicked_callback(reset_button, self.on_reset)

        save_button = r.Rwidget_add_child(footer, r.Rbutton_create("Save"))
        r.Rbutton_set_style(save_button, r.R_BUTTON_PRIMARY)
        r.Rbutton_set_clicked_callback(save_button, self.on_save)

        self.rebuild()
        return self.root

    # -- public API -------------------------------------------------------

    def save(self):
        config = self.get_config()
        ok, error = settings.save(config.path, config)
        if not ok:
            self.report(error)
            return False
        config.loaded = True
        config.error = ""
        config.settings = self.get_schema().setting_count()
        self.report("Saved %s. Toolbar and first pane apply at the next start." % config.path)
        return True

    def reset(self):
        config = self.get_config()
        fresh = settings.defaults(config.path)
        config.__dict__.update(fresh.__dict__)
        self.on_changed()
        self.rebuild()
        self.report("Defaults restored in memory - Save writes them.")

    def set_filter(self, text):
        self.filter = text or ""
        r.Rtext_input_set_text(self.search, self.filter)
        self.rebuild()

    def shown_count(self):
        return self.shown
