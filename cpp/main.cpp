/**
 * Raven example - workbench.
 *
 * The shell of a desktop tool, with an application pane and the instrument
 * panel of Examples/InstrumentPanel:
 *
 *   - a standard menu bar (File, View, Help) with shortcuts;
 *   - a toolbar that the configuration file can hide;
 *   - a side bar listing the panes;
 *   - a central Switcher: an Application page (name, version, release, config)
 *     and the instrument panel; neither is rebuilt when the other is shown;
 *   - a status bar.
 *
 * The settings come from a JSONC file (JSON with comments), found in this
 * order: `--config <path>`, the WORKBENCH_CONFIG variable, then the
 * `workbench.jsonc` next to this source file. Missing keys keep their default.
 *
 *   ./workbench                          the application
 *   ./workbench --config mine.jsonc      another configuration
 *   ./workbench --test                   no window, no GPU: checks only
 *   ./workbench --capture shot.ppm       a frame, saved as a PPM image
 *   ./workbench --capture shot.ppm --pane application
 *
 * The panel itself is ../../InstrumentPanel/cpp/panel.cpp.
 */

#include <Raven.h>
#include <raven_web.h>

#include "config.h"
#include "panel.h"
#include "preferences.h"
#include "schema.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#ifndef WORKBENCH_VERSION
#define WORKBENCH_VERSION "0.1.0"
#endif
#ifndef WORKBENCH_BUILD_DATE
#define WORKBENCH_BUILD_DATE "unknown"
#endif
#ifndef WORKBENCH_DEFAULT_CONFIG
#define WORKBENCH_DEFAULT_CONFIG "workbench.jsonc"
#endif
#ifndef WORKBENCH_HELP_DIR
#define WORKBENCH_HELP_DIR "help"
#endif
#ifndef WORKBENCH_SCHEMA
#define WORKBENCH_SCHEMA "workbench.schema.json"
#endif

enum Pane {
	PANE_APPLICATION = 0,
	PANE_PANEL = 1,
	PANE_HELP = 2,
	PANE_PREFERENCES = 3
};

struct App {
	Rid window = R_INVALID_ID;
	Rid switcher = R_INVALID_ID;
	Rid status = R_INVALID_ID;
	Rid status_right = R_INVALID_ID;

	Rid toolbar = R_INVALID_ID;
	Rid toolbar_panel = R_INVALID_ID;
	Rid pause = R_INVALID_ID;

	Rid sidebar_app = R_INVALID_ID;
	Rid sidebar_panel = R_INVALID_ID;
	Rid sidebar_help = R_INVALID_ID;

	/* the documentation pane: a WebView, created the first time it is opened */
	Rid help_page = R_INVALID_ID;
	Rid help_view = R_INVALID_ID;
	Rid help_back = R_INVALID_ID;
	Rid help_forward = R_INVALID_ID;
	Rid help_reload = R_INVALID_ID;
	Rid help_status = R_INVALID_ID;
	bool help_loaded = false;
	std::string help_status_text;

	Rid app_item = R_INVALID_ID;
	Rid panel_item = R_INVALID_ID;

	int pane = PANE_APPLICATION;
	Panel* panel = nullptr;
	Config config;
	Schema schema;
	Preferences* preferences = nullptr;
};

static App g;

/* ---------------------------------------------------------------------------
 *  Shell state
 * ------------------------------------------------------------------------- */

static void set_status(const char* text) {
	Rlabel_set_text(g.status, text);
}

static void update_right_status() {
	char text[200];
	if (g.pane == PANE_APPLICATION) {
		std::snprintf(text, sizeof(text), "Raven Workbench %s   %s", WORKBENCH_VERSION, g.config.channel.c_str());
	} else {
		std::snprintf(text, sizeof(text), "P-101 %5.1f bar   F-201 %5.1f L/min   %s%s",
			(double)panel_pressure(g.panel), (double)panel_flow(g.panel),
			panel_running(g.panel) ? "running" : "paused",
			panel_alarm(g.panel) ? "   ALARM" : "");
	}
	Rlabel_set_text(g.status_right, text);
}

static void sync_shell() {
	Rmenu_item_set_checked(g.app_item, g.pane == PANE_APPLICATION ? R_TRUE : R_FALSE);
	Rmenu_item_set_checked(g.panel_item, g.pane == PANE_PANEL ? R_TRUE : R_FALSE);
	Rbutton_set_style(g.sidebar_app, g.pane == PANE_APPLICATION ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	Rbutton_set_style(g.sidebar_panel, g.pane == PANE_PANEL ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	Rbutton_set_style(g.sidebar_help, g.pane == PANE_HELP ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	if (g.toolbar_panel != R_INVALID_ID) {
		Rbutton_set_style(g.toolbar_panel, g.pane == PANE_PANEL ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	}
	if (g.pause != R_INVALID_ID) {
		Rbutton_set_text(g.pause, panel_running(g.panel) ? "Pause" : "Resume");
	}
	update_right_status();
}

static std::string help_file(const char* page) {
	return std::string(WORKBENCH_HELP_DIR) + "/" + page;
}

/** The embedded browser starts with the first help page, not at startup. */
static void ensure_help() {
	if (g.help_view != R_INVALID_ID) return;
	g.help_view = Rweb_view_create();
	Rwidget_add_child(g.help_page, g.help_view);
	Rslot_set_fill(g.help_view, 1.0f);
}

static void load_help(const char* page) {
	ensure_help();
	/* A file URL carrying the theme in use: the page styles itself from
	 * ?theme=dark, whichever theme the desktop prefers. Spaces are the only
	 * characters this path escapes. */
	const std::string path = help_file(page);
	std::string url = "file://";
	for (char c : path) {
		if (c == ' ') url += "%20";
		else url += c;
	}
	url += Rtheme_is_dark() ? "?theme=dark" : "?theme=light";
	Rweb_view_load_url(g.help_view, url.c_str());
	g.help_loaded = true;
}

static void update_help_status() {
	if (g.help_view == R_INVALID_ID) return;

	const char* error = Rweb_view_get_error(g.help_view);
	const char* title = Rweb_view_get_title(g.help_view);
	char text[200];
	if (error && error[0]) {
		std::snprintf(text, sizeof(text), "Documentation unavailable");
	} else if (Rweb_view_is_loading(g.help_view)) {
		std::snprintf(text, sizeof(text), "Loading...");
	} else {
		std::snprintf(text, sizeof(text), "%s", title && title[0] ? title : "User guide");
	}
	if (g.help_status_text != text) {
		g.help_status_text = text;
		Rlabel_set_text(g.help_status, text);
		Rwindow_request_redraw(g.window);
	}
	Rwidget_set_enabled(g.help_back, Rweb_view_can_go_back(g.help_view) ? R_TRUE : R_FALSE);
	Rwidget_set_enabled(g.help_forward, Rweb_view_can_go_forward(g.help_view) ? R_TRUE : R_FALSE);
}

static void set_pane(int pane) {
	g.pane = (pane == PANE_PANEL || pane == PANE_HELP || pane == PANE_PREFERENCES) ? pane
		: PANE_APPLICATION;
	if (g.pane == PANE_HELP) {
		ensure_help();
		if (!g.help_loaded) load_help("index.html");
	}
	Rswitcher_set_index(g.switcher, g.pane);
	sync_shell();
	if (g.pane == PANE_APPLICATION) set_status("Application information.");
	else if (g.pane == PANE_PANEL) set_status("Instrument panel opened.");
	else if (g.pane == PANE_HELP) set_status("User guide.");
	else set_status("Preferences.");
}

static void toggle_panel() {
	set_pane(g.pane == PANE_PANEL ? PANE_APPLICATION : PANE_PANEL);
}

/* ---------------------------------------------------------------------------
 *  Actions and buttons
 * ------------------------------------------------------------------------- */

static void on_view_action(Rid sender, const char* action, void* user_data) {
	(void)user_data;
	if (std::strcmp(action, "view.application") == 0) {
		set_pane(PANE_APPLICATION);
	} else if (std::strcmp(action, "view.panel") == 0) {
		/* The checkable item toggled itself before firing: read it. */
		set_pane(Rmenu_item_is_checked(sender) != 0 ? PANE_PANEL : PANE_APPLICATION);
	}
}

static void on_pause_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)action;
	(void)user_data;
	panel_set_running(g.panel, !panel_running(g.panel));
	sync_shell();
	set_status(panel_running(g.panel) ? "Simulation running." : "Simulation paused.");
}

static void on_reset_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)action;
	(void)user_data;
	panel_reset(g.panel);
	panel_set_running(g.panel, true);
	sync_shell();
	set_status("Plant reset to a cold start.");
}

static void on_about_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)action;
	(void)user_data;
	char message[512];
	std::snprintf(message, sizeof(message),
		"Raven Workbench %s (%s)\n\nBuild %s\nRaven %d.%d.%d\n\nConfiguration:\n  %s\n  %s",
		WORKBENCH_VERSION, g.config.channel.c_str(), WORKBENCH_BUILD_DATE,
		RAVEN_VERSION_MAJOR, RAVEN_VERSION_MINOR, RAVEN_VERSION_PATCH,
		g.config.path.c_str(), g.config.status().c_str());
	Rmessage_box(g.window, "About Workbench", message, R_MESSAGE_OK);
	set_status("About closed.");
}

static void on_toggle_button(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	toggle_panel();
}

static void on_pause_button(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	panel_set_running(g.panel, !panel_running(g.panel));
	sync_shell();
	set_status(panel_running(g.panel) ? "Simulation running." : "Simulation paused.");
}

static void on_reset_button(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	panel_reset(g.panel);
	panel_set_running(g.panel, true);
	sync_shell();
	set_status("Plant reset to a cold start.");
}

static void on_alarms_button(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	set_status("Alarms is a placeholder: the panel's alarm lamp is the real one.");
}

static void on_clipboard_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)user_data;
	char text[600];
	if (std::strcmp(action, "clipboard.version") == 0) {
		std::snprintf(text, sizeof(text), "Raven Workbench %s (%s)", WORKBENCH_VERSION, g.config.channel.c_str());
	} else if (std::strcmp(action, "clipboard.config") == 0) {
		std::snprintf(text, sizeof(text), "%s", g.config.path.c_str());
	} else {
		std::snprintf(text, sizeof(text), "P-101 %.1f bar   F-201 %.1f L/min",
			(double)panel_pressure(g.panel), (double)panel_flow(g.panel));
	}
	Rclipboard_set_text(text);
	set_status("Copied to the clipboard.");
}

static void on_preferences_command(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)user_data;
	if (!g.preferences) return;
	if (std::strcmp(action, "preferences.save") == 0) preferences_save(g.preferences);
	else if (std::strcmp(action, "preferences.reset") == 0) preferences_reset(g.preferences);
}

static void on_help_command(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)user_data;
	if (g.help_view == R_INVALID_ID) return;
	if (std::strcmp(action, "help.reload") == 0) Rweb_view_reload(g.help_view);
	else if (std::strcmp(action, "help.back") == 0) Rweb_view_go_back(g.help_view);
	else if (std::strcmp(action, "help.forward") == 0) Rweb_view_go_forward(g.help_view);
}

static void on_edit_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)user_data;
	if (std::strcmp(action, "edit.preferences") == 0) set_pane(PANE_PREFERENCES);
}

static void on_help_action(Rid sender, const char* action, void* user_data) {
	(void)sender;
	(void)user_data;
	set_pane(PANE_HELP);
	if (std::strcmp(action, "help.config") == 0) load_help("configuration.html");
	else load_help("index.html");
}

static void on_help_back(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	if (g.help_view != R_INVALID_ID) Rweb_view_go_back(g.help_view);
}

static void on_help_forward(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	if (g.help_view != R_INVALID_ID) Rweb_view_go_forward(g.help_view);
}

static void on_help_reload(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	if (g.help_view != R_INVALID_ID) Rweb_view_reload(g.help_view);
}

static void on_sidebar_help(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	set_pane(PANE_HELP);
}

/* ---------------------------------------------------------------------------
 *  Frame
 * ------------------------------------------------------------------------- */

static void on_frame(double delta, void* user_data) {
	(void)user_data;
	/* The help pane updates its own title from the browser. */
	if (g.pane == PANE_HELP) {
		update_help_status();
		return;
	}
	/* The application page is static; only the panel costs frames. */
	if (g.pane != PANE_PANEL) return;

	panel_simulate(g.panel, delta);
	panel_refresh(g.panel);
	update_right_status();
	if (panel_alarm(g.panel)) set_status("HIGH PRESSURE - the interlock is armed.");
	Rwindow_request_redraw(g.window);
}

/* ---------------------------------------------------------------------------
 *  UI
 * ------------------------------------------------------------------------- */

static void on_sidebar_application(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	set_pane(PANE_APPLICATION);
}

static void on_sidebar_panel(Rid button, void* user_data) {
	(void)button;
	(void)user_data;
	set_pane(PANE_PANEL);
}

static Rid info_row(Rid parent, const char* key, const char* value, const char* name = nullptr) {
	Rid row = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(row, 12.0f);

	Rid label = Rwidget_add_child(row, Rlabel_create(key));
	Rlabel_set_text_size(label, 12.0f);
	Rwidget_set_opacity(label, 0.55f);
	Rwidget_set_min_size(label, 110.0f, 0.0f);

	Rid text = Rwidget_add_child(row, Rlabel_create(value));
	Rlabel_set_text_size(text, 12.0f);
	Rslot_set_fill(text, 1.0f);
	if (name) Rwidget_set_name(text, name);
	return text;
}

static Rid build_application_page() {
	Rid page = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(page, 8.0f);
	Rslot_set_padding(page, 36.0f, 32.0f, 36.0f, 28.0f);

	Rid title = Rwidget_add_child(page, Rlabel_create("Raven Workbench"));
	Rlabel_set_text_size(title, 30.0f);

	char release[200];
	std::snprintf(release, sizeof(release), "Release %s   ·   channel %s   ·   built %s",
		WORKBENCH_VERSION, g.config.channel.c_str(), WORKBENCH_BUILD_DATE);
	Rid subtitle = Rwidget_add_child(page, Rlabel_create(release));
	Rlabel_set_text_size(subtitle, 13.0f);
	Rwidget_set_opacity(subtitle, 0.65f);

	Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

	char version[100];
	std::snprintf(version, sizeof(version), "%s (built %s)", WORKBENCH_VERSION, WORKBENCH_BUILD_DATE);
	char raven[32];
	std::snprintf(raven, sizeof(raven), "%d.%d.%d", RAVEN_VERSION_MAJOR, RAVEN_VERSION_MINOR, RAVEN_VERSION_PATCH);

	info_row(page, "Application", "Raven Workbench");
	info_row(page, "Version", version, "info.version");
	info_row(page, "Channel", g.config.channel.c_str(), "info.channel");
	info_row(page, "Raven", raven);
	char config_line[600];
	std::snprintf(config_line, sizeof(config_line), "%s", g.config.path.c_str());
	info_row(page, "Configuration", config_line, "info.config");
	info_row(page, "Config state", g.config.status().c_str(), "info.state");

	Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

	Rid shortcuts = Rwidget_add_child(page, Rlabel_create("Panes and shortcuts"));
	Rlabel_set_text_size(shortcuts, 11.0f);
	Rwidget_set_opacity(shortcuts, 0.5f);
	info_row(page, "Ctrl+I", "Instrument panel");
	info_row(page, "Ctrl+0", "Application");
	info_row(page, "Ctrl+Q", "Quit");

	Rid spacer = Rwidget_add_child(page, Rwidget_create(R_WIDGET_SPACER));
	Rslot_set_fill(spacer, 1.0f);

	Rid footer = Rwidget_add_child(page, Rlabel_create("Examples/Workbench · part of the Raven repository"));
	Rlabel_set_text_size(footer, 10.0f);
	Rwidget_set_opacity(footer, 0.35f);

	return page;
}

static Rid tool_button(Rid bar, const char* text, const char* tooltip,
	RButtonClickedCallback callback) {
	Rid button = Rwidget_add_child(bar, Rbutton_create(text));
	Rbutton_set_clicked_callback(button, callback, nullptr);
	Rwidget_set_tooltip(button, tooltip);
	return button;
}

static Rid sidebar_button(Rid column, const char* text, const char* tooltip,
	RButtonClickedCallback callback) {
	Rid button = Rwidget_add_child(column, Rbutton_create(text));
	Rbutton_set_clicked_callback(button, callback, nullptr);
	Rwidget_set_tooltip(button, tooltip);
	/* No Slot.Fill in a VBox: that would be a share of the *height*. */
	Rslot_set_halign(button, R_HALIGN_FILL);
	return button;
}

/* What the Preferences pane changes that can be applied without a restart. */
static void apply_live_settings() {
	Rtheme_apply(g.config.theme == "light" ? R_THEME_LIGHT : R_THEME_DARK);
	panel_set_alarm_limit(g.panel, g.config.alarm_limit);
	panel_set_window(g.panel, g.config.chart_seconds);
	panel_set_switch(g.panel, PANEL_PUMP, g.config.pump);
	panel_set_switch(g.panel, PANEL_VALVE, g.config.valve);
	panel_set_switch(g.panel, PANEL_INTERLOCKS, g.config.armed);
	update_right_status();
	Rwindow_request_redraw(g.window);
}

static void on_settings_changed(void* user) {
	(void)user;
	apply_live_settings();
}

static void on_settings_status(void* user, const char* text) {
	(void)user;
	set_status(text);
}

static Rid build_help_page() {
	Rid page = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(page, 0.0f);

	Rid bar = Rwidget_add_child(page, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(bar, 6.0f);
	Rslot_set_padding(bar, 8.0f, 6.0f, 8.0f, 6.0f);

	g.help_back = tool_button(bar, "Back", "Previous page", on_help_back);
	g.help_forward = tool_button(bar, "Forward", "Next page", on_help_forward);
	g.help_reload = tool_button(bar, "Reload", "Reload the page", on_help_reload);
	Rwidget_set_enabled(g.help_back, R_FALSE);
	Rwidget_set_enabled(g.help_forward, R_FALSE);

	Rid spacer = Rwidget_add_child(bar, Rwidget_create(R_WIDGET_SPACER));
	Rslot_set_fill(spacer, 1.0f);

	g.help_status = Rwidget_add_child(bar, Rlabel_create("User guide"));
	Rlabel_set_text_size(g.help_status, 11.0f);
	Rwidget_set_opacity(g.help_status, 0.6f);
	Rslot_set_valign(g.help_status, R_VALIGN_CENTER);

	Rwidget_add_child(page, Rwidget_create(R_WIDGET_SEPARATOR));

	/* The WebView is added here the first time the pane is opened. */
	g.help_page = Rwidget_add_child(page, Rwidget_create(R_WIDGET_VBOX));
	Rslot_set_fill(g.help_page, 1.0f);
	return page;
}

static Rid context_menu(const char* const* labels, const char* const* actions, int count) {
	Rid menu = Rwidget_create(R_WIDGET_MENU);
	for (int i = 0; i < count; i++) {
		if (!labels[i]) {
			Rmenu_add_separator(menu);
			continue;
		}
		Rid item = Rmenu_add_item(menu, labels[i]);
		if (actions[i]) Rwidget_set_action(item, actions[i]);
	}
	return menu;
}

/* One menu per area: Raven walks up from the widget under the pointer, so a
 * menu set on a pane is the menu of everything inside it. */
static void attach_context_menus(Rid sidebar, Rid application_page, Rid panel_page,
	Rid preferences_page, Rid help_page) {
	static const char* const sidebar_labels[] = {"Application", "Instrument panel", "User guide", nullptr, "Preferences..."};
	static const char* const sidebar_actions[] = {"view.application", "view.panel", "help.guide", nullptr, "edit.preferences"};

	static const char* const application_labels[] = {"User guide", "Preferences...", nullptr,
		"Copy version", "Copy configuration path"};
	static const char* const application_actions[] = {"help.guide", "edit.preferences", nullptr,
		"clipboard.version", "clipboard.config"};

	static const char* const panel_labels[] = {"Pause the simulation", "Reset the plant", nullptr,
		"Copy the readings"};
	static const char* const panel_actions[] = {"plant.pause", "plant.reset", nullptr,
		"clipboard.readings"};

	static const char* const preferences_labels[] = {"Save", "Reset to defaults", nullptr,
		"Copy configuration path"};
	static const char* const preferences_actions[] = {"preferences.save", "preferences.reset", nullptr,
		"clipboard.config"};

	static const char* const help_labels[] = {"Back", "Forward", "Reload", nullptr, "Preferences..."};
	static const char* const help_actions[] = {"help.back", "help.forward", "help.reload", nullptr,
		"edit.preferences"};

	Rwidget_set_context_menu(sidebar,
		context_menu(sidebar_labels, sidebar_actions, 5));
	Rwidget_set_context_menu(application_page,
		context_menu(application_labels, application_actions, 5));
	Rwidget_set_context_menu(panel_page,
		context_menu(panel_labels, panel_actions, 4));
	Rwidget_set_context_menu(preferences_page,
		context_menu(preferences_labels, preferences_actions, 4));
	Rwidget_set_context_menu(help_page,
		context_menu(help_labels, help_actions, 5));
}

static void build_window() {
	/* A second window (the test builds two) must not keep the old handles. */
	g.window = g.switcher = g.status = g.status_right = R_INVALID_ID;
	g.toolbar = g.toolbar_panel = g.pause = R_INVALID_ID;
	g.sidebar_app = g.sidebar_panel = g.sidebar_help = R_INVALID_ID;
	g.app_item = g.panel_item = R_INVALID_ID;
	if (g.preferences) {
		preferences_destroy(g.preferences);
		g.preferences = nullptr;
	}
	g.help_page = g.help_view = g.help_status = R_INVALID_ID;
	g.help_back = g.help_forward = g.help_reload = R_INVALID_ID;
	g.help_loaded = false;
	g.help_status_text.clear();

	Rtheme_apply(g.config.theme == "light" ? R_THEME_LIGHT : R_THEME_DARK);
	g.window = Rwindow_create("Raven Workbench", 1280, 820, R_WINDOW_DEFAULT);

	Rid root = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(root, 0.0f);
	Rwindow_set_content(g.window, root);

	/* --- the menu bar ------------------------------------------------------ */
	Rid bar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_MENU_BAR));

	Rid file = Rmenu_add_menu(bar, "File");
	Rid quit = Rmenu_add_item(file, "Quit");
	Rmenu_item_set_shortcut(quit, R_KEY_Q, R_MOD_CTRL);
	Rwidget_set_action(quit, "app.quit");

	Rid edit = Rmenu_add_menu(bar, "Edit");
	Rid preferences_item = Rmenu_add_item(edit, "Preferences...");
	Rmenu_item_set_shortcut(preferences_item, R_KEY_COMMA, R_MOD_CTRL);
	Rwidget_set_action(preferences_item, "edit.preferences");

	Rid view = Rmenu_add_menu(bar, "View");
	g.app_item = Rmenu_add_item(view, "Application");
	Rmenu_item_set_checkable(g.app_item, R_TRUE);
	Rmenu_item_set_shortcut(g.app_item, R_KEY_0, R_MOD_CTRL);
	Rwidget_set_action(g.app_item, "view.application");

	g.panel_item = Rmenu_add_item(view, "Instrument panel");
	Rmenu_item_set_checkable(g.panel_item, R_TRUE);
	Rmenu_item_set_shortcut(g.panel_item, R_KEY_I, R_MOD_CTRL);
	Rwidget_set_action(g.panel_item, "view.panel");

	Rmenu_add_separator(view);
	Rid pause_item = Rmenu_add_item(view, "Pause the simulation");
	Rwidget_set_action(pause_item, "plant.pause");
	Rid reset_item = Rmenu_add_item(view, "Reset the plant");
	Rwidget_set_action(reset_item, "plant.reset");
	Rmenu_add_separator(view);
	Rid dark = Rmenu_add_item(view, "Dark");
	Rwidget_set_action(dark, "theme.dark");
	Rid light = Rmenu_add_item(view, "Light");
	Rwidget_set_action(light, "theme.light");

	Rid help = Rmenu_add_menu(bar, "Help");
	Rid guide = Rmenu_add_item(help, "User guide");
	Rmenu_item_set_shortcut(guide, R_KEY_F1, 0);
	Rwidget_set_action(guide, "help.guide");
	Rmenu_add_separator(help);
	Rid about = Rmenu_add_item(help, "About Workbench...");
	Rwidget_set_action(about, "help.about");

	/* --- the toolbar, when the configuration wants one --------------------- */
	if (g.config.toolbar) {
		g.toolbar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
		Rpanel_set_spacing(g.toolbar, 6.0f);
		Rslot_set_padding(g.toolbar, 8.0f, 6.0f, 8.0f, 6.0f);

		g.toolbar_panel = tool_button(g.toolbar, "Instrument panel",
			"Open the instrument panel  (Ctrl+I)", on_toggle_button);
		g.pause = tool_button(g.toolbar, "Pause", "Pause or resume the simulated plant",
			on_pause_button);
		tool_button(g.toolbar, "Reset", "Reset the plant and the trend", on_reset_button);

		Rid spacer = Rwidget_add_child(g.toolbar, Rwidget_create(R_WIDGET_SPACER));
		Rslot_set_fill(spacer, 1.0f);

		Rid plant = Rwidget_add_child(g.toolbar, Rlabel_create("SIMULATED PLANT"));
		Rlabel_set_text_size(plant, 11.0f);
		Rwidget_set_opacity(plant, 0.55f);
		Rslot_set_valign(plant, R_VALIGN_CENTER);
	} else {
		/* The layout still needs a pause widget: the status bar has one. */
		g.pause = R_INVALID_ID;
	}

	/* --- the body ---------------------------------------------------------- */
	Rid body = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(body, 0.0f);
	Rslot_set_fill(body, 1.0f);

	Rid sidebar = Rwidget_add_child(body, Rwidget_create(R_WIDGET_VBOX));
	Rpanel_set_spacing(sidebar, 6.0f);
	Rslot_set_padding(sidebar, 12.0f, 12.0f, 12.0f, 12.0f);
	Rwidget_set_min_size(sidebar, 190.0f, 0.0f);

	Rid panes = Rwidget_add_child(sidebar, Rlabel_create("PANES"));
	Rlabel_set_text_size(panes, 10.0f);
	Rwidget_set_opacity(panes, 0.6f);

	g.sidebar_app = sidebar_button(sidebar, "Application", "Application information  (Ctrl+0)",
		on_sidebar_application);
	g.sidebar_panel = sidebar_button(sidebar, "Instrument panel", "Open the instrument panel  (Ctrl+I)",
		on_sidebar_panel);
	g.sidebar_help = sidebar_button(sidebar, "User guide", "This documentation  (F1)", on_sidebar_help);
	sidebar_button(sidebar, "Alarms", "A placeholder pane", on_alarms_button);

	Rid sidebar_spacer = Rwidget_add_child(sidebar, Rwidget_create(R_WIDGET_SPACER));
	Rslot_set_fill(sidebar_spacer, 1.0f);

	Rid version = Rwidget_add_child(sidebar, Rlabel_create("Raven Workbench\n" WORKBENCH_VERSION));
	Rlabel_set_text_size(version, 10.0f);
	Rwidget_set_opacity(version, 0.45f);

	Rid divider = Rwidget_add_child(body, Rwidget_create(R_WIDGET_SEPARATOR));
	Rseparator_set_orientation(divider, R_VERTICAL);

	g.switcher = Rwidget_add_child(body, Rwidget_create(R_WIDGET_SWITCHER));
	Rslot_set_fill(g.switcher, 1.0f);
	Rid application_page = build_application_page();
	Rwidget_add_child(g.switcher, application_page);

	Rid pane = panel_build(g.panel);
	Rslot_set_padding(pane, 16.0f, 16.0f, 16.0f, 16.0f);
	Rwidget_add_child(g.switcher, pane);

	Rid help_page = build_help_page();
	Rwidget_add_child(g.switcher, help_page);

	g.preferences = preferences_create(&g.config, &g.schema,
		PreferencesCallbacks{on_settings_changed, on_settings_status, nullptr});
	Rid preferences_page = preferences_build(g.preferences);
	Rslot_set_padding(preferences_page, 16.0f, 16.0f, 16.0f, 16.0f);
	Rwidget_add_child(g.switcher, preferences_page);

	/* A panel is transparent to the pointer by default; these areas show a menu,
	 * so they must be hit. Children still take the clicks first. */
	Rwidget_set_hit_testable(sidebar, R_TRUE);
	Rwidget_set_hit_testable(application_page, R_TRUE);
	Rwidget_set_hit_testable(pane, R_TRUE);
	Rwidget_set_hit_testable(preferences_page, R_TRUE);
	Rwidget_set_hit_testable(help_page, R_TRUE);

	attach_context_menus(sidebar, application_page, pane, preferences_page, help_page);

	/* --- the status bar ---------------------------------------------------- */
	Rwidget_add_child(root, Rwidget_create(R_WIDGET_SEPARATOR));
	Rid status_bar = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(status_bar, 12.0f);
	Rslot_set_padding(status_bar, 12.0f, 5.0f, 12.0f, 5.0f);

	g.status = Rwidget_add_child(status_bar, Rlabel_create(g.config.status().c_str()));
	Rlabel_set_text_size(g.status, 11.0f);
	Rslot_set_fill(g.status, 1.0f);
	Rslot_set_valign(g.status, R_VALIGN_CENTER);

	g.status_right = Rwidget_add_child(status_bar, Rlabel_create(""));
	Rlabel_set_text_size(g.status_right, 11.0f);
	Rwidget_set_opacity(g.status_right, 0.7f);
	Rslot_set_valign(g.status_right, R_VALIGN_CENTER);

	/* --- actions ----------------------------------------------------------- */
	Raction_bind("view.application", on_view_action, nullptr);
	Raction_bind("view.panel", on_view_action, nullptr);
	Raction_bind("plant.pause", on_pause_action, nullptr);
	Raction_bind("plant.reset", on_reset_action, nullptr);
	Raction_bind("edit.preferences", on_edit_action, nullptr);
	Raction_bind("clipboard.version", on_clipboard_action, nullptr);
	Raction_bind("clipboard.config", on_clipboard_action, nullptr);
	Raction_bind("clipboard.readings", on_clipboard_action, nullptr);
	Raction_bind("preferences.save", on_preferences_command, nullptr);
	Raction_bind("preferences.reset", on_preferences_command, nullptr);
	Raction_bind("help.reload", on_help_command, nullptr);
	Raction_bind("help.back", on_help_command, nullptr);
	Raction_bind("help.forward", on_help_command, nullptr);
	Raction_bind("help.guide", on_help_action, nullptr);
	Raction_bind("help.config", on_help_action, nullptr);
	Raction_bind("help.about", on_about_action, nullptr);

	/* --- apply the configuration ------------------------------------------ */
	panel_set_alarm_limit(g.panel, g.config.alarm_limit);
	panel_set_window(g.panel, g.config.chart_seconds);
	panel_set_switch(g.panel, PANEL_PUMP, g.config.pump);
	panel_set_switch(g.panel, PANEL_VALVE, g.config.valve);
	panel_set_switch(g.panel, PANEL_INTERLOCKS, g.config.armed);

	set_pane(g.config.pane);
	/* the first message says where the settings came from */
	set_status(g.config.status().c_str());
	update_right_status();
}

/* ---------------------------------------------------------------------------
 *  Start
 * ------------------------------------------------------------------------- */

static bool init(bool headless) {
	RInitDesc desc{};
	desc.backend = headless ? R_BACKEND_NULL : R_BACKEND_VULKAN;
	desc.headless = headless ? R_TRUE : R_FALSE;
	desc.app_name = "Raven workbench";
	if (!Rinit_ex(&desc)) {
		std::fprintf(stderr, "cannot start Raven (is a Vulkan driver installed?)\n");
		return false;
	}
	return true;
}

/** Moves the pointer to the middle of a widget and clicks it. */
static void click(Rid widget) {
	if (widget == R_INVALID_ID) return;
	float x = 0, y = 0, width = 0, height = 0;
	Rwidget_get_rect(widget, &x, &y, &width, &height);
	Rwindow_inject_mouse_move(g.window, x + width * 0.5f, y + height * 0.5f);
	Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_TRUE);
	Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_FALSE);
}

static bool save_ppm(Rid window, const char* path) {
	int width = 0, height = 0;
	uint8_t* pixels = Rwindow_capture(window, &width, &height);
	FILE* file = pixels ? std::fopen(path, "wb") : nullptr;
	if (!file) {
		Rpixels_free(pixels);
		return false;
	}
	std::fprintf(file, "P6\n%d %d\n255\n", width, height);
	for (int i = 0; i < width * height; i++) std::fwrite(pixels + i * 4, 1, 3, file);
	std::fclose(file);
	Rpixels_free(pixels);
	return true;
}

static int run_tests() {
	int failures = 0;
#define CHECK(condition, message) do { \
	if (!(condition)) { std::printf("  FAIL: %s\n", message); failures++; } \
} while (0)

	/* --- the JSONC reader -------------------------------------------------- */
	const char* config_path = "/tmp/raven_workbench_test.jsonc";
	{
		FILE* file = std::fopen(config_path, "wb");
		std::fputs(
			"{\n"
			"  // a hand-written configuration\n"
			"  \"release\": { \"channel\": \"beta\" },\n"
			"  \"ui\": { \"theme\": \"light\", \"toolbar\": false, },\n"
			"  /* the pane to open at startup */\n"
			"  \"startup\": { \"pane\": \"instrument-panel\", \"pump\": true, \"valve\": true },\n"
			"  \"simulation\": { \"alarm_limit\": 50, \"chart_seconds\": 5 },\n"
			"}\n", file);
		std::fclose(file);
	}
	Config custom = config_load(config_path);
	CHECK(custom.loaded, "the JSONC file loads (comments and trailing commas)");
	CHECK(custom.channel == "beta", "release.channel");
	CHECK(custom.theme == "light", "ui.theme");
	CHECK(!custom.toolbar, "ui.toolbar");
	CHECK(custom.pane == 1, "startup.pane");
	CHECK(custom.pump && custom.valve && !custom.armed, "startup switches");
	CHECK(custom.alarm_limit == 50.0, "simulation.alarm_limit");
	CHECK(custom.chart_seconds == 5.0, "simulation.chart_seconds");
	CHECK(custom.settings == 8, "eight settings applied");

	{
		FILE* file = std::fopen(config_path, "wb");
		std::fputs("{ \"ui\": { \"theme\": } }", file);
		std::fclose(file);
	}
	Config broken = config_load(config_path);
	CHECK(!broken.loaded && !broken.error.empty(), "a syntax error is reported");
	CHECK(broken.channel == "dev", "a broken file keeps the defaults");
	CHECK(config_load("/tmp/raven_workbench_absent.jsonc").error.empty(), "a missing file is not an error");

	/* --- the default application ------------------------------------------ */
	g.config = config_load(WORKBENCH_DEFAULT_CONFIG);
	build_window();
	Rdraw_frame();

	CHECK(g.pane == PANE_APPLICATION, "the application pane is the default");
	CHECK(Rswitcher_get_index(g.switcher) == PANE_APPLICATION, "the switcher shows it");
	CHECK(g.toolbar != R_INVALID_ID, "the toolbar is shown by default");
	CHECK(panel_window(g.panel) == 10.0, "the chart window defaults to 10 s");
	CHECK(panel_alarm_limit(g.panel) == 75.0, "the alarm defaults to 75 bar");

	{
		Rid version = Rwidget_find(g.window, "info.version");
		Rid channel = Rwidget_find(g.window, "info.channel");
		Rid config_label = Rwidget_find(g.window, "info.config");
		CHECK(Rwidget_is_valid(version) && std::strstr(Rlabel_get_text(version), WORKBENCH_VERSION),
			"the application pane shows the version");
		CHECK(Rwidget_is_valid(channel) && std::strcmp(Rlabel_get_text(channel), "dev") == 0,
			"the application pane shows the channel");
		CHECK(Rwidget_is_valid(config_label) &&
			std::strcmp(Rlabel_get_text(config_label), WORKBENCH_DEFAULT_CONFIG) == 0,
			"the application pane shows the configuration path");
	}

	/* the toolbar opens the panel, the side bar goes back */
	click(g.toolbar_panel);
	CHECK(g.pane == PANE_PANEL, "the toolbar opens the instrument panel");
	CHECK(Rswitcher_get_index(g.switcher) == PANE_PANEL, "the switcher follows");
	CHECK(Rmenu_item_is_checked(g.panel_item), "the View item follows the toolbar");
	click(g.sidebar_app);
	CHECK(g.pane == PANE_APPLICATION, "the side bar goes back to the application");

	/* the menu does the same, through the action the item fires */
	Rmenu_item_set_checked(g.panel_item, R_TRUE);
	Raction_fire("view.panel", g.panel_item);
	CHECK(g.pane == PANE_PANEL, "View > Instrument panel opens it");
	Rmenu_item_set_checked(g.app_item, R_TRUE);
	Raction_fire("view.application", g.app_item);
	CHECK(g.pane == PANE_APPLICATION, "View > Application goes back");

	/* the panel runs, and a hidden pane costs nothing */
	set_pane(PANE_PANEL);
	for (int i = 0; i < 90; i++) panel_simulate(g.panel, 1.0 / 60.0);
	const int samples = panel_samples(g.panel);
	CHECK(samples > 0, "the panel collected samples");
	set_pane(PANE_APPLICATION);
	on_frame(1.0, nullptr);
	CHECK(panel_samples(g.panel) == samples, "a hidden pane does not simulate");

	/* pause and reset from the toolbar */
	set_pane(PANE_PANEL);
	const bool was_running = panel_running(g.panel);
	click(g.pause);
	CHECK(panel_running(g.panel) != was_running, "the toolbar Pause toggles the plant");
	click(g.pause);
	Raction_fire("plant.reset", R_INVALID_ID);
	CHECK(panel_samples(g.panel) == 0, "Reset empties the trend");
	CHECK(std::strcmp(Rlabel_get_text(g.status), "Plant reset to a cold start.") == 0,
		"the status bar names the last command");

	/* --- the documentation pane ------------------------------------------- */
	Raction_fire("help.guide", R_INVALID_ID);
	CHECK(g.pane == PANE_HELP, "Help > User guide opens the documentation pane");
	CHECK(Rswitcher_get_index(g.switcher) == PANE_HELP, "the switcher shows it");
	CHECK(Rwidget_is_valid(g.help_view), "the embedded browser view exists");
	CHECK(!Rweb_is_available(), "the test runs without a browser");
	{
		const char* error = Rweb_view_get_error(g.help_view);
		CHECK(error && error[0], "the view explains that the browser is missing");
	}
	Raction_fire("help.config", R_INVALID_ID);
	CHECK(g.pane == PANE_HELP, "Help > Configuration stays in the pane");
	click(g.sidebar_app);
	CHECK(g.pane == PANE_APPLICATION, "the side bar returns to the application");

	/* --- the preferences editor ------------------------------------------- */
	CHECK(g.schema.loaded, "the schema loads");
	CHECK((int)g.schema.groups.size() == 4, "four groups in the schema");
	CHECK(g.schema.setting_count() == 9, "nine settings in the schema");

	Raction_fire("edit.preferences", R_INVALID_ID);
	CHECK(g.pane == PANE_PREFERENCES, "Edit > Preferences opens the pane");
	CHECK(Rswitcher_get_index(g.switcher) == PANE_PREFERENCES, "the switcher shows it");
	CHECK(preferences_shown(g.preferences) == 9, "every setting has a row");

	preferences_set_filter(g.preferences, "trend");
	CHECK(preferences_shown(g.preferences) == 1, "the search filters the rows");
	preferences_set_filter(g.preferences, "");
	CHECK(preferences_shown(g.preferences) == 9, "clearing the search shows them all");

	{
		Rid toolbar = Rwidget_find(g.window, "setting.ui.toolbar");
		CHECK(Rwidget_is_valid(toolbar), "the toolbar setting has a control");
		const bool before = g.config.toolbar;
		click(toolbar);
		CHECK(g.config.toolbar != before, "clicking the control changes the setting");
	}

	{
		/* a hand-edited file keeps its comments when the settings change */
		const char* path = "/tmp/raven_workbench_comments.jsonc";
		FILE* file = std::fopen(path, "wb");
		std::fputs(
			"{\n"
			"  // keep this comment\n"
			"  \"release\": { \"channel\": \"dev\" },\n"
			"  \"ui\": {\n"
			"    \"theme\": \"dark\", // inline comment\n"
			"    \"toolbar\": true\n"
			"  },\n"
			"  \"startup\": { \"pane\": \"application\", \"pump\": false, \"valve\": false },\n"
			"  \"simulation\": { \"alarm_limit\": 75, \"chart_seconds\": 10 }\n"
			"}\n", file);
		std::fclose(file);

		Config edited = config_load(path);
		CHECK(edited.loaded, "a commented file loads");
		edited.theme = "light";
		edited.armed = true;   /* missing from the file: it must be added */
		std::string error;
		CHECK(config_save(path, edited, error), "save edits the file in place");
		std::string text;
		{
			std::ifstream input(path, std::ios::binary);
			std::ostringstream buffer;
			buffer << input.rdbuf();
			text = buffer.str();
		}
		CHECK(text.find("// keep this comment") != std::string::npos, "comments survive a save");
		CHECK(text.find("// inline comment") != std::string::npos, "an inline comment survives a save");
		CHECK(text.find("\"theme\": \"light\"") != std::string::npos, "the value was replaced");
		CHECK(text.find("\"armed\": true") != std::string::npos, "a missing key was added");
		Config reread = config_load(path);
		CHECK(reread.theme == "light" && reread.armed, "the edited file reads back");
		std::remove(path);
	}

	{
		/* save where the test may write, then read the file back */
		g.config.channel = "stable";
		g.config.path = "/tmp/raven_workbench_saved.jsonc";
		CHECK(preferences_save(g.preferences), "Save writes the configuration");
		Config reloaded = config_load(g.config.path);
		CHECK(reloaded.loaded, "the written file loads");
		CHECK(reloaded.channel == "stable", "the change reached the file");
		preferences_reset(g.preferences);
		CHECK(g.config.channel == "dev", "Reset restores the defaults");
		std::remove("/tmp/raven_workbench_saved.jsonc");
	}

	click(g.sidebar_app);
	CHECK(g.pane == PANE_APPLICATION, "the side bar leaves the preferences");

	/* --- context menus ----------------------------------------------------- */
	{
		set_pane(PANE_PANEL);
		Rdraw_frame();
		const float row = Rtheme_get_metric(R_METRIC_ROW_HEIGHT);
		const float bar = Rtheme_get_metric(R_METRIC_CONTROL_HEIGHT);
		(void)bar;

		/* the side bar has a menu; its first item goes back to the application */
		Rwindow_inject_mouse_move(g.window, 60.0f, 420.0f);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_TRUE);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_FALSE);
		CHECK(Rwindow_has_popup(g.window) != 0, "right-clicking the side bar opens a menu");
		Rwindow_inject_mouse_move(g.window, 90.0f, 420.0f + 4.0f + row * 0.5f);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_TRUE);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_FALSE);
		CHECK(g.pane == PANE_APPLICATION, "the side bar menu opens the application pane");
		CHECK(Rwindow_has_popup(g.window) == 0, "the menu closes after the choice");

		/* Escape closes a menu */
		Rwindow_inject_mouse_move(g.window, 60.0f, 420.0f);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_TRUE);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_FALSE);
		CHECK(Rwindow_has_popup(g.window) != 0, "the menu opens again");
		Rwindow_inject_key(g.window, R_KEY_ESCAPE, R_TRUE, 0);
		Rwindow_inject_key(g.window, R_KEY_ESCAPE, R_FALSE, 0);
		CHECK(Rwindow_has_popup(g.window) == 0, "Escape closes it");

		/* the instrument panel has a menu too: its first item pauses the plant */
		set_pane(PANE_PANEL);
		Rdraw_frame();
		const bool was_running = panel_running(g.panel);
		Rwindow_inject_mouse_move(g.window, 600.0f, 500.0f);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_TRUE);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_FALSE);
		CHECK(Rwindow_has_popup(g.window) != 0, "right-clicking the panel opens a menu");
		Rwindow_inject_mouse_move(g.window, 630.0f, 500.0f + 4.0f + row * 0.5f);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_TRUE);
		Rwindow_inject_mouse_button(g.window, R_MOUSE_LEFT, R_FALSE);
		CHECK(panel_running(g.panel) != was_running, "the panel menu pauses the simulation");
		panel_set_running(g.panel, true);  /* leave it running for the rest of the test */
	}

	/* the clipboard actions behind the menus */
	Raction_fire("clipboard.version", R_INVALID_ID);
	{
		const char* copied = Rclipboard_get_text();
		CHECK(copied && std::strstr(copied, WORKBENCH_VERSION) != nullptr,
			"Copy version puts the version on the clipboard");
	}

	/* --- a configured application ----------------------------------------- */
	Rwindow_destroy(g.window);
	g.config = custom;
	build_window();
	Rdraw_frame();

	CHECK(g.toolbar == R_INVALID_ID, "ui.toolbar = false hides the toolbar");
	CHECK(g.pane == PANE_PANEL, "startup.pane opens the instrument panel");
	CHECK(Rswitcher_get_index(g.switcher) == PANE_PANEL, "the switcher shows the panel");
	CHECK(panel_alarm_limit(g.panel) == 50.0, "simulation.alarm_limit reaches the panel");
	CHECK(panel_window(g.panel) == 5.0, "simulation.chart_seconds reaches the chart");
	CHECK(panel_switch(g.panel, PANEL_PUMP) && panel_switch(g.panel, PANEL_VALVE),
		"startup switches are on");
	CHECK(!Rtheme_is_dark(), "ui.theme = light");

	{
		Rid channel = Rwidget_find(g.window, "info.channel");
		Rid state = Rwidget_find(g.window, "info.state");
		CHECK(Rwidget_is_valid(channel) && std::strcmp(Rlabel_get_text(channel), "beta") == 0,
			"the application pane shows the configured channel");
		CHECK(Rwidget_is_valid(state) && std::strstr(Rlabel_get_text(state), "8 settings") != nullptr,
			"the application pane reports the loaded settings");
	}

	/* the pump from the configuration raises the pressure by itself */
	const float start_pressure = panel_pressure(g.panel);
	for (int i = 0; i < 180; i++) panel_simulate(g.panel, 1.0 / 60.0);
	CHECK(panel_pressure(g.panel) > start_pressure + 20.0f, "the configured pump runs the plant");

	std::printf("workbench: %d checks failed, %d samples, config %s\n", failures,
		panel_samples(g.panel), custom.status().c_str());
	Rshutdown();
	std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}

static std::string config_path(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) return argv[i + 1];
	}
	if (const char* from_env = std::getenv("WORKBENCH_CONFIG")) return from_env;
	return WORKBENCH_DEFAULT_CONFIG;
}

int main(int argc, char** argv) {
	bool test = false;
	const char* capture = nullptr;
	const char* pane_override = nullptr;
	bool show_menu = false;
	int frames = 120;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--test") == 0) test = true;
		else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
		else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
		else if (std::strcmp(argv[i], "--pane") == 0 && i + 1 < argc) pane_override = argv[++i];
		else if (std::strcmp(argv[i], "--menu") == 0) show_menu = true;
	}

	if (!init(test)) return 1;
	/* The test does not start a browser; the view reports it, the pane still works. */
	if (test) Rweb_set_browser_path("/nonexistent-browser");
	g.schema = schema_load(WORKBENCH_SCHEMA);
	g.panel = panel_create();
	g.config = config_load(config_path(argc, argv));
	if (pane_override) {
		if (std::strcmp(pane_override, "instrument-panel") == 0) g.config.pane = PANE_PANEL;
		else if (std::strcmp(pane_override, "user-guide") == 0) g.config.pane = PANE_HELP;
		else if (std::strcmp(pane_override, "preferences") == 0) g.config.pane = PANE_PREFERENCES;
		else g.config.pane = PANE_APPLICATION;
	}

	int code = 0;
	if (test) {
		code = run_tests();
	} else {
		build_window();
		if (capture) {
			/* keep the real pointer off the menus, and show the plant running */
			Rwindow_inject_mouse_move(g.window, 4.0f, 700.0f);
			Rdraw_frame();
			if (g.pane == PANE_PANEL) {
				if (!panel_switch(g.panel, PANEL_PUMP)) click(panel_button(g.panel, PANEL_PUMP));
				if (!panel_switch(g.panel, PANEL_VALVE)) click(panel_button(g.panel, PANEL_VALVE));
				if (!panel_switch(g.panel, PANEL_INTERLOCKS)) click(panel_button(g.panel, PANEL_INTERLOCKS));
			}
			for (int i = 0; i < frames; i++) {
				on_frame(1.0 / 60.0, nullptr);
				Rwindow_request_redraw(g.window);
				/* a little time for the embedded browser to answer */
				Rwait_frame(0.01);
			}
			if (show_menu) {
				/* right-click a panel, for the screenshot of a context menu */
				float x = 60.0f, y = 420.0f;
				if (g.pane == PANE_PANEL) { x = 620.0f; y = 470.0f; }
				else if (g.pane == PANE_APPLICATION) { x = 60.0f; y = 420.0f; }  /* the side bar */
				Rwindow_inject_mouse_move(g.window, x, y);
				Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_TRUE);
				Rwindow_inject_mouse_button(g.window, R_MOUSE_RIGHT, R_FALSE);
				for (int i = 0; i < 5; i++) {
					Rwindow_request_redraw(g.window);
					Rwait_frame(0.02);
				}
			}
			if (!save_ppm(g.window, capture)) {
				std::fprintf(stderr, "cannot write %s\n", capture);
				code = 1;
			} else {
				std::printf("saved %s\n", capture);
			}
			Rshutdown();
		} else {
			Rset_frame_callback(on_frame, nullptr);
			while (Rwait_frame(-1.0)) {
			}
			Rshutdown();
		}
	}

	panel_destroy(g.panel);
	return code;
}
