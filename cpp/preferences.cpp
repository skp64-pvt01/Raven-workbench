/**
 * The Preferences pane - see preferences.h.
 */

#include "preferences.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

struct Binding {
	Preferences* owner = nullptr;
	std::string path;
	std::string title;
	std::vector<std::string> choices;
};

struct Preferences {
	Config* config = nullptr;
	const Schema* schema = nullptr;
	PreferencesCallbacks callbacks{};

	Rid root = R_INVALID_ID;
	Rid search = R_INVALID_ID;
	Rid list = R_INVALID_ID;
	Rid scroll = R_INVALID_ID;
	Rid meta = R_INVALID_ID;
	Rid status = R_INVALID_ID;
	Rid save = R_INVALID_ID;
	Rid reset = R_INVALID_ID;

	std::string filter;
	std::vector<std::unique_ptr<Binding>> bindings;
	int shown = 0;
};

static void rebuild(Preferences* preferences);

/* ---------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------- */

static std::string lowered(std::string text) {
	for (char& c : text) c = (char)std::tolower((unsigned char)c);
	return text;
}

static bool matches(const std::string& haystack, const std::string& needle) {
	return lowered(haystack).find(needle) != std::string::npos;
}

static void report(Preferences* preferences, const std::string& text) {
	if (preferences->callbacks.status) {
		preferences->callbacks.status(preferences->callbacks.user, text.c_str());
	}
}

static void changed(Preferences* preferences, const Binding* binding) {
	if (preferences->callbacks.changed) preferences->callbacks.changed(preferences->callbacks.user);
	report(preferences, "Changed \"" + binding->title + "\" - not saved yet.");
}

/* ---------------------------------------------------------------------------
 *  Controls
 * ------------------------------------------------------------------------- */

static void on_check(Rid widget, int checked, void* data) {
	(void)widget;
	Binding* binding = static_cast<Binding*>(data);
	if (!binding) return;
	config_set(*binding->owner->config, binding->path, checked ? "true" : "false");
	changed(binding->owner, binding);
}

static void on_choice(Rid widget, int index, void* data) {
	(void)widget;
	Binding* binding = static_cast<Binding*>(data);
	if (!binding || index < 0 || index >= (int)binding->choices.size()) return;
	config_set(*binding->owner->config, binding->path, binding->choices[index]);
	changed(binding->owner, binding);
}

static void on_number(Rid widget, double value, void* data) {
	(void)widget;
	Binding* binding = static_cast<Binding*>(data);
	if (!binding) return;
	char text[32];
	std::snprintf(text, sizeof(text), "%g", value);
	config_set(*binding->owner->config, binding->path, text);
	changed(binding->owner, binding);
}

static void on_text(Rid widget, const char* text, void* data) {
	(void)widget;
	Binding* binding = static_cast<Binding*>(data);
	if (!binding) return;
	config_set(*binding->owner->config, binding->path, text ? text : "");
	changed(binding->owner, binding);
}

static Rid make_control(Preferences* preferences, const SchemaSetting& setting, Binding* binding) {
	std::string value;
	if (!config_get(*preferences->config, setting.path, value)) {
		/* The schema names a setting this build does not use. */
		Rid note = Rlabel_create("not used by this build");
		Rlabel_set_text_size(note, 11.0f);
		Rwidget_set_opacity(note, 0.5f);
		return note;
	}

	Rid control = R_INVALID_ID;
	if (setting.type == "boolean") {
		control = Rcheckbox_create("");
		Rcheckbox_set_checked(control, value == "true" ? R_TRUE : R_FALSE);
		Rcheckbox_set_changed_callback(control, on_check, binding);
	} else if (!setting.choices.empty()) {
		control = Rwidget_create(R_WIDGET_COMBO_BOX);
		for (const auto& choice : setting.choices) Rcombo_box_add_item(control, choice.c_str());
		int selected = 0;
		for (int i = 0; i < (int)setting.choices.size(); i++) {
			if (setting.choices[i] == value) selected = i;
		}
		Rcombo_box_set_selected(control, selected);
		Rcombo_box_set_changed_callback(control, on_choice, binding);
	} else if (setting.type == "number" || setting.type == "integer") {
		const bool integer = setting.type == "integer";
		control = Rwidget_create(R_WIDGET_SPIN_BOX);
		Rspin_box_set_range(control, setting.has_range ? setting.minimum : 0.0,
			setting.has_range ? setting.maximum : 100.0);
		Rspin_box_set_step(control, integer ? 1.0 : 0.5);
		Rspin_box_set_decimals(control, integer ? 0 : 1);
		Rspin_box_set_value(control, std::atof(value.c_str()));
		Rspin_box_set_changed_callback(control, on_number, binding);
	} else {
		control = Rwidget_create(R_WIDGET_TEXT_INPUT);
		Rtext_input_set_text(control, value.c_str());
		Rtext_input_set_changed_callback(control, on_text, binding);
	}

	/* The text column takes the slack; the control keeps its size at the right.
	 * A check box is small, so it must not be given the width of a combo box. */
	if (setting.type != "boolean") Rwidget_set_min_size(control, 220.0f, 0.0f);
	Rslot_set_halign(control, R_HALIGN_RIGHT);
	Rslot_set_valign(control, R_VALIGN_CENTER);
	return control;
}

/* ---------------------------------------------------------------------------
 *  Rows
 * ------------------------------------------------------------------------- */

static void add_group_label(Preferences* preferences, const std::string& title) {
	Rid label = Rwidget_add_child(preferences->list, Rlabel_create(title.c_str()));
	Rlabel_set_text_size(label, 12.0f);
	Rwidget_set_opacity(label, 0.6f);
	Rslot_set_padding(label, 0.0f, 14.0f, 0.0f, 4.0f);
}

static void add_row(Preferences* preferences, const SchemaSetting& setting) {
	Rid row = Rwidget_add_child(preferences->list, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(row, 16.0f);
	Rslot_set_padding(row, 0.0f, 6.0f, 0.0f, 6.0f);

	Rid text = Rwidget_add_child(row, Rwidget_create(R_WIDGET_VBOX));
	Rpanel_set_spacing(text, 2.0f);
	Rslot_set_fill(text, 1.0f);

	Rid title = Rwidget_add_child(text, Rlabel_create(setting.title.c_str()));
	Rlabel_set_text_size(title, 13.0f);

	if (!setting.description.empty()) {
		Rid description = Rwidget_add_child(text, Rlabel_create(setting.description.c_str()));
		Rlabel_set_text_size(description, 11.0f);
		Rwidget_set_opacity(description, 0.55f);
		Rlabel_set_wrap(description, R_TRUE);
	}

	preferences->bindings.push_back(std::make_unique<Binding>());
	Binding* binding = preferences->bindings.back().get();
	binding->owner = preferences;
	binding->path = setting.path;
	binding->title = setting.title;
	binding->choices = setting.choices;

	Rid control = make_control(preferences, setting, binding);
	Rwidget_add_child(row, control);
	const std::string name = "setting." + setting.path;
	Rwidget_set_name(control, name.c_str());
}

static void rebuild(Preferences* preferences) {
	if (preferences->list != R_INVALID_ID) {
		Rwidget_destroy(preferences->list);
		preferences->list = R_INVALID_ID;
	}
	preferences->bindings.clear();
	preferences->shown = 0;

	preferences->list = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(preferences->list, 2.0f);
	Rwidget_add_child(preferences->scroll, preferences->list);
	Rslot_set_fill(preferences->list, 1.0f);

	const std::string needle = lowered(preferences->filter);
	for (const auto& group : preferences->schema->groups) {
		bool header = false;
		for (const auto& setting : group.settings) {
			const bool keep = needle.empty() || matches(setting.title, needle) ||
				matches(setting.description, needle) || matches(setting.path, needle);
			if (!keep) continue;
			if (!header) {
				add_group_label(preferences, group.title);
				header = true;
			}
			add_row(preferences, setting);
			preferences->shown++;
		}
	}

	char meta[900];
	if (preferences->schema->loaded) {
		std::snprintf(meta, sizeof(meta), "config  %s\nschema  %s",
			preferences->config->path.c_str(), preferences->schema->path.c_str());
	} else {
		std::snprintf(meta, sizeof(meta), "config  %s\nschema  %s  (%s)",
			preferences->config->path.c_str(), preferences->schema->path.c_str(),
			preferences->schema->error.c_str());
	}
	Rlabel_set_text(preferences->meta, meta);

	char shown[64];
	std::snprintf(shown, sizeof(shown), "Showing %d of %d settings",
		preferences->shown, preferences->schema->setting_count());
	Rlabel_set_text(preferences->status, shown);
}

/* ---------------------------------------------------------------------------
 *  Buttons
 * ------------------------------------------------------------------------- */

static void on_search(Rid widget, const char* text, void* data) {
	(void)widget;
	Preferences* preferences = static_cast<Preferences*>(data);
	preferences->filter = text ? text : "";
	rebuild(preferences);
}

static void on_reset(Rid button, void* data) {
	(void)button;
	Preferences* preferences = static_cast<Preferences*>(data);
	const std::string path = preferences->config->path;
	*preferences->config = config_defaults(path);
	if (preferences->callbacks.changed) preferences->callbacks.changed(preferences->callbacks.user);
	rebuild(preferences);
	report(preferences, "Defaults restored in memory - Save writes them.");
}

static void on_save(Rid button, void* data) {
	(void)button;
	preferences_save(static_cast<Preferences*>(data));
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

Preferences* preferences_create(Config* config, const Schema* schema, PreferencesCallbacks callbacks) {
	Preferences* preferences = new Preferences();
	preferences->config = config;
	preferences->schema = schema;
	preferences->callbacks = callbacks;
	return preferences;
}

void preferences_destroy(Preferences* preferences) {
	delete preferences;
}

Rid preferences_build(Preferences* preferences) {
	preferences->root = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(preferences->root, 10.0f);
	Rslot_set_padding(preferences->root, 24.0f, 20.0f, 24.0f, 16.0f);

	Rid header = Rwidget_add_child(preferences->root, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(header, 12.0f);

	Rid title = Rwidget_add_child(header, Rlabel_create("Preferences"));
	Rlabel_set_text_size(title, 20.0f);
	Rslot_set_valign(title, R_VALIGN_CENTER);

	Rid spacer = Rwidget_add_child(header, Rwidget_create(R_WIDGET_SPACER));
	Rslot_set_fill(spacer, 1.0f);

	preferences->search = Rwidget_add_child(header, Rwidget_create(R_WIDGET_TEXT_INPUT));
	Rtext_input_set_placeholder(preferences->search, "Search settings");
	Rwidget_set_min_size(preferences->search, 260.0f, 0.0f);
	Rslot_set_valign(preferences->search, R_VALIGN_CENTER);
	Rtext_input_set_changed_callback(preferences->search, on_search, preferences);

	preferences->meta = Rwidget_add_child(preferences->root, Rlabel_create(""));
	Rlabel_set_text_size(preferences->meta, 11.0f);
	Rwidget_set_opacity(preferences->meta, 0.55f);

	Rwidget_add_child(preferences->root, Rwidget_create(R_WIDGET_SEPARATOR));

	preferences->scroll = Rwidget_add_child(preferences->root, Rwidget_create(R_WIDGET_SCROLL_BOX));
	Rslot_set_fill(preferences->scroll, 1.0f);

	Rwidget_add_child(preferences->root, Rwidget_create(R_WIDGET_SEPARATOR));

	Rid footer = Rwidget_add_child(preferences->root, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(footer, 8.0f);

	preferences->status = Rwidget_add_child(footer, Rlabel_create(""));
	Rlabel_set_text_size(preferences->status, 11.0f);
	Rwidget_set_opacity(preferences->status, 0.6f);
	Rslot_set_fill(preferences->status, 1.0f);
	Rslot_set_valign(preferences->status, R_VALIGN_CENTER);

	preferences->reset = Rwidget_add_child(footer, Rbutton_create("Reset to defaults"));
	Rbutton_set_clicked_callback(preferences->reset, on_reset, preferences);

	preferences->save = Rwidget_add_child(footer, Rbutton_create("Save"));
	Rbutton_set_style(preferences->save, R_BUTTON_PRIMARY);
	Rbutton_set_clicked_callback(preferences->save, on_save, preferences);

	rebuild(preferences);
	return preferences->root;
}

bool preferences_save(Preferences* preferences) {
	std::string error;
	if (!config_save(preferences->config->path, *preferences->config, error)) {
		report(preferences, error);
		return false;
	}
	preferences->config->loaded = true;
	preferences->config->error.clear();
	preferences->config->settings = preferences->schema ? preferences->schema->setting_count() : 0;
	report(preferences, "Saved " + preferences->config->path +
		". Toolbar and first pane apply at the next start.");
	return true;
}

void preferences_reset(Preferences* preferences) {
	const std::string path = preferences->config->path;
	*preferences->config = config_defaults(path);
	if (preferences->callbacks.changed) preferences->callbacks.changed(preferences->callbacks.user);
	rebuild(preferences);
	report(preferences, "Defaults restored in memory - Save writes them.");
}

void preferences_set_filter(Preferences* preferences, const char* text) {
	preferences->filter = text ? text : "";
	Rtext_input_set_text(preferences->search, preferences->filter.c_str());
	rebuild(preferences);
}

int preferences_shown(const Preferences* preferences) {
	return preferences->shown;
}
