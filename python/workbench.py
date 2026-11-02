"""Raven Workbench (Python) - a desktop application shell on Raven.

A menu bar, a toolbar, a side bar, a status bar and four panes (Application,
Instrument panel, User guide, Preferences); a JSONC configuration file with a
JSON Schema that drives the Preferences form; an embedded user guide and
right-click menus.

    python3 workbench.py                     the application
    python3 workbench.py --test              no window, no GPU: checks only
    python3 workbench.py --capture shot.ppm  one frame, saved as a PPM
    python3 workbench.py --config mine.jsonc another settings file

Needs the Raven shared library (RAVEN_LIBRARY) and the binding
(raven.py in bindings/python), exactly like the Python course.
"""

import os
import sys
from pathlib import Path

import raven as r

import config as settings
import panel
import preferences
import schema as schema_module
import web

VERSION = "0.1.0"
ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = str(ROOT / "workbench.jsonc")
SCHEMA_PATH = str(ROOT / "workbench.schema.json")
HELP_DIR = str(ROOT / "help")

PANE_APPLICATION = 0
PANE_PANEL = 1
PANE_HELP = 2
PANE_PREFERENCES = 3


class App:
    def __init__(self):
        self.window = r.R_INVALID_ID
        self.switcher = r.R_INVALID_ID
        self.status = r.R_INVALID_ID
        self.status_right = r.R_INVALID_ID

        self.toolbar = r.R_INVALID_ID
        self.toolbar_panel = r.R_INVALID_ID
        self.pause = r.R_INVALID_ID

        self.sidebar_app = r.R_INVALID_ID
        self.sidebar_panel = r.R_INVALID_ID
        self.sidebar_help = r.R_INVALID_ID

        self.help_page = r.R_INVALID_ID
        self.help_view = r.R_INVALID_ID
        self.help_back = r.R_INVALID_ID
        self.help_forward = r.R_INVALID_ID
        self.help_reload = r.R_INVALID_ID
        self.help_status = r.R_INVALID_ID
        self.help_loaded = False
        self.help_status_text = ""

        self.app_item = r.R_INVALID_ID
        self.panel_item = r.R_INVALID_ID

        self.pane = PANE_APPLICATION
        self.config = settings.defaults(DEFAULT_CONFIG)
        self.schema = schema_module.Schema(SCHEMA_PATH)
        self.preferences = None


APP = App()


# ---------------------------------------------------------------------------
#  Shell state
# ---------------------------------------------------------------------------


def set_status(text):
    r.Rlabel_set_text(APP.status, text)


def update_right_status():
    if APP.pane == PANE_APPLICATION:
        text = "Raven Workbench %s   %s" % (VERSION, APP.config.channel)
    else:
        text = "P-101 %5.1f bar   F-201 %5.1f L/min   %s%s" % (
            panel.PANEL.pressure,
            panel.PANEL.flow,
            "running" if panel.PANEL.running else "paused",
            "   ALARM" if panel.alarm_active() else "",
        )
    r.Rlabel_set_text(APP.status_right, text)


def sync_shell():
    r.Rmenu_item_set_checked(APP.app_item, APP.pane == PANE_APPLICATION)
    r.Rmenu_item_set_checked(APP.panel_item, APP.pane == PANE_PANEL)
    r.Rbutton_set_style(
        APP.sidebar_app,
        r.R_BUTTON_PRIMARY if APP.pane == PANE_APPLICATION else r.R_BUTTON_DEFAULT,
    )
    r.Rbutton_set_style(
        APP.sidebar_panel,
        r.R_BUTTON_PRIMARY if APP.pane == PANE_PANEL else r.R_BUTTON_DEFAULT,
    )
    r.Rbutton_set_style(
        APP.sidebar_help,
        r.R_BUTTON_PRIMARY if APP.pane == PANE_HELP else r.R_BUTTON_DEFAULT,
    )
    if APP.toolbar_panel != r.R_INVALID_ID:
        r.Rbutton_set_style(
            APP.toolbar_panel,
            r.R_BUTTON_PRIMARY if APP.pane == PANE_PANEL else r.R_BUTTON_DEFAULT,
        )
    if APP.pause != r.R_INVALID_ID:
        r.Rbutton_set_text(APP.pause, "Pause" if panel.PANEL.running else "Resume")
    update_right_status()


def help_file(page):
    return os.path.join(HELP_DIR, page)


def ensure_help():
    if APP.help_view != r.R_INVALID_ID or APP.help_loaded:
        return
    if not web.AVAILABLE:
        # The generated binding stops at Raven.h; a library without raven_web
        # still runs everything except the guide.
        note = r.Rwidget_add_child(APP.help_page, r.Rlabel_create(
            "This Raven library was built without raven_web: the guide is in help/."))
        r.Rlabel_set_wrap(note, 1)
        APP.help_loaded = True
        return
    APP.help_view = web.view_create()
    r.Rwidget_add_child(APP.help_page, APP.help_view)
    r.Rslot_set_fill(APP.help_view, 1)


def load_help(page):
    ensure_help()
    if APP.help_view == r.R_INVALID_ID:
        return
    path = help_file(page).replace(" ", "%20")
    url = "file://%s%s" % (path, "?theme=dark" if r.Rtheme_is_dark() else "?theme=light")
    web.load_url(APP.help_view, url)
    APP.help_loaded = True


def update_help_status():
    if APP.help_view == r.R_INVALID_ID:
        return
    error = web.get_error(APP.help_view)
    if error:
        text = "Documentation unavailable"
    elif web.is_loading(APP.help_view):
        text = "Loading..."
    else:
        text = web.get_title(APP.help_view) or "User guide"
    if APP.help_status_text != text:
        APP.help_status_text = text
        r.Rlabel_set_text(APP.help_status, text)
        r.Rwindow_request_redraw(APP.window)
    r.Rwidget_set_enabled(APP.help_back, web.can_go_back(APP.help_view))
    r.Rwidget_set_enabled(APP.help_forward, web.can_go_forward(APP.help_view))


def set_pane(pane):
    APP.pane = pane if pane in (PANE_PANEL, PANE_HELP, PANE_PREFERENCES) else PANE_APPLICATION
    if APP.pane == PANE_HELP:
        ensure_help()
        if not APP.help_loaded:
            load_help("index.html")
    r.Rswitcher_set_index(APP.switcher, APP.pane)
    sync_shell()
    if APP.pane == PANE_APPLICATION:
        set_status("Application information.")
    elif APP.pane == PANE_PANEL:
        set_status("Instrument panel opened.")
    elif APP.pane == PANE_HELP:
        set_status("User guide.")
    else:
        set_status("Preferences.")


def toggle_panel():
    set_pane(PANE_APPLICATION if APP.pane == PANE_PANEL else PANE_PANEL)


# ---------------------------------------------------------------------------
#  Actions
# ---------------------------------------------------------------------------


def on_action(sender, action, user_data):
    if action == "view.application":
        set_pane(PANE_APPLICATION)
    elif action == "view.panel":
        # The checkable item toggled itself before firing: read it.
        set_pane(PANE_PANEL if r.Rmenu_item_is_checked(sender) else PANE_APPLICATION)
    elif action == "plant.pause":
        panel.PANEL.running = not panel.PANEL.running
        sync_shell()
        set_status("Simulation running." if panel.PANEL.running else "Simulation paused.")
    elif action == "plant.reset":
        panel.reset()
        panel.PANEL.running = True
        sync_shell()
        set_status("Plant reset to a cold start.")
    elif action == "edit.preferences":
        set_pane(PANE_PREFERENCES)
    elif action == "clipboard.version":
        r.Rclipboard_set_text("Raven Workbench %s (%s)" % (VERSION, APP.config.channel))
        set_status("Copied to the clipboard.")
    elif action == "clipboard.config":
        r.Rclipboard_set_text(APP.config.path)
        set_status("Copied to the clipboard.")
    elif action == "clipboard.readings":
        r.Rclipboard_set_text("P-101 %.1f bar   F-201 %.1f L/min"
                              % (panel.PANEL.pressure, panel.PANEL.flow))
        set_status("Copied to the clipboard.")
    elif action == "preferences.save":
        if APP.preferences:
            APP.preferences.save()
    elif action == "preferences.reset":
        if APP.preferences:
            APP.preferences.reset()
    elif action == "help.reload":
        if APP.help_view != r.R_INVALID_ID:
            web.reload(APP.help_view)
    elif action == "help.back":
        if APP.help_view != r.R_INVALID_ID:
            web.go_back(APP.help_view)
    elif action == "help.forward":
        if APP.help_view != r.R_INVALID_ID:
            web.go_forward(APP.help_view)
    elif action == "help.guide":
        set_pane(PANE_HELP)
        load_help("index.html")
    elif action == "help.config":
        set_pane(PANE_HELP)
        load_help("configuration.html")
    elif action == "help.about":
        message = (
            "Raven Workbench %s (%s)\n\nRaven %d.%d.%d\n\nConfiguration:\n  %s\n  %s"
            % (VERSION, APP.config.channel, r.RAVEN_VERSION_MAJOR, r.RAVEN_VERSION_MINOR,
               r.RAVEN_VERSION_PATCH, APP.config.path, APP.config.status())
        )
        r.Rmessage_box(APP.window, "About Workbench", message, r.R_MESSAGE_OK)
        set_status("About closed.")
    elif action == "theme.dark":
        APP.config.theme = "dark"
        apply_live_settings()
        set_status("Dark theme. Saved with Preferences.")
    elif action == "theme.light":
        APP.config.theme = "light"
        apply_live_settings()
        set_status("Light theme. Saved with Preferences.")


# ---------------------------------------------------------------------------
#  Frame
# ---------------------------------------------------------------------------


def on_frame(delta, user_data):
    # The help pane updates its own title from the browser.
    if APP.pane == PANE_HELP:
        update_help_status()
        return
    # The application page is static; only the panel costs frames.
    if APP.pane != PANE_PANEL:
        return

    panel.simulate(delta)
    panel.refresh()
    update_right_status()
    if panel.alarm_active():
        set_status("HIGH PRESSURE - the interlock is armed.")
    r.Rwindow_request_redraw(APP.window)


# ---------------------------------------------------------------------------
#  UI
# ---------------------------------------------------------------------------


def on_sidebar_application(button, user_data):
    set_pane(PANE_APPLICATION)


def on_sidebar_panel(button, user_data):
    set_pane(PANE_PANEL)


def on_sidebar_help(button, user_data):
    set_pane(PANE_HELP)


def on_alarms(button, user_data):
    set_status("Alarms is a placeholder: the panel's alarm lamp is the real one.")


def on_toggle_button(button, user_data):
    toggle_panel()


def on_pause_button(button, user_data):
    panel.PANEL.running = not panel.PANEL.running
    sync_shell()
    set_status("Simulation running." if panel.PANEL.running else "Simulation paused.")


def on_reset_button(button, user_data):
    panel.reset()
    panel.PANEL.running = True
    sync_shell()
    set_status("Plant reset to a cold start.")


def on_help_back(button, user_data):
    if APP.help_view != r.R_INVALID_ID:
        web.go_back(APP.help_view)


def on_help_forward(button, user_data):
    if APP.help_view != r.R_INVALID_ID:
        web.go_forward(APP.help_view)


def on_help_reload(button, user_data):
    if APP.help_view != r.R_INVALID_ID:
        web.reload(APP.help_view)


def info_row(parent, key, value, name=None):
    row = r.Rwidget_add_child(parent, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(row, 12)

    label = r.Rwidget_add_child(row, r.Rlabel_create(key))
    r.Rlabel_set_text_size(label, 12)
    r.Rwidget_set_opacity(label, 0.55)
    r.Rwidget_set_min_size(label, 110, 0)

    text = r.Rwidget_add_child(row, r.Rlabel_create(value))
    r.Rlabel_set_text_size(text, 12)
    r.Rslot_set_fill(text, 1)
    if name:
        r.Rwidget_set_name(text, name)
    return text


def build_application_page():
    page = r.Rwidget_create(r.R_WIDGET_VBOX)
    r.Rpanel_set_spacing(page, 8)
    r.Rslot_set_padding(page, 36, 32, 36, 28)

    title = r.Rwidget_add_child(page, r.Rlabel_create("Raven Workbench"))
    r.Rlabel_set_text_size(title, 30)

    subtitle = r.Rwidget_add_child(
        page,
        r.Rlabel_create("Release %s   ·   channel %s" % (VERSION, APP.config.channel)))
    r.Rlabel_set_text_size(subtitle, 13)
    r.Rwidget_set_opacity(subtitle, 0.65)

    r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_SEPARATOR))

    info_row(page, "Application", "Raven Workbench")
    info_row(page, "Version", "%s (Python build)" % VERSION, "info.version")
    info_row(page, "Channel", APP.config.channel, "info.channel")
    info_row(page, "Raven", "%d.%d.%d" % (r.RAVEN_VERSION_MAJOR, r.RAVEN_VERSION_MINOR,
                                          r.RAVEN_VERSION_PATCH))
    info_row(page, "Configuration", APP.config.path, "info.config")
    info_row(page, "Config state", APP.config.status(), "info.state")

    r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_SEPARATOR))

    shortcuts = r.Rwidget_add_child(page, r.Rlabel_create("Panes and shortcuts"))
    r.Rlabel_set_text_size(shortcuts, 11)
    r.Rwidget_set_opacity(shortcuts, 0.5)
    info_row(page, "Ctrl+I", "Instrument panel")
    info_row(page, "Ctrl+0", "Application")
    info_row(page, "Ctrl+Q", "Quit")

    spacer = r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_SPACER))
    r.Rslot_set_fill(spacer, 1)

    footer = r.Rwidget_add_child(
        page, r.Rlabel_create("Python implementation · part of the Raven Workbench repository"))
    r.Rlabel_set_text_size(footer, 10)
    r.Rwidget_set_opacity(footer, 0.35)
    return page


def tool_button(bar, text, tooltip, callback):
    button = r.Rwidget_add_child(bar, r.Rbutton_create(text))
    r.Rbutton_set_clicked_callback(button, callback)
    r.Rwidget_set_tooltip(button, tooltip)
    return button


def sidebar_button(column, text, tooltip, callback):
    button = tool_button(column, text, tooltip, callback)
    # No Slot.Fill in a VBox: that would be a share of the *height*.
    r.Rslot_set_halign(button, r.R_HALIGN_FILL)
    return button


def apply_live_settings():
    r.Rtheme_apply(r.R_THEME_LIGHT if APP.config.theme == "light" else r.R_THEME_DARK)
    panel.set_alarm_limit(APP.config.alarm_limit)
    panel.set_window(APP.config.chart_seconds)
    panel.set_switch(panel.PANEL_PUMP, APP.config.pump)
    panel.set_switch(panel.PANEL_VALVE, APP.config.valve)
    panel.set_switch(panel.PANEL_INTERLOCKS, APP.config.armed)
    update_right_status()
    r.Rwindow_request_redraw(APP.window)


def build_help_page():
    page = r.Rwidget_create(r.R_WIDGET_VBOX)
    r.Rpanel_set_spacing(page, 0)

    bar = r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(bar, 6)
    r.Rslot_set_padding(bar, 8, 6, 8, 6)

    APP.help_back = tool_button(bar, "Back", "Previous page", on_help_back)
    APP.help_forward = tool_button(bar, "Forward", "Next page", on_help_forward)
    APP.help_reload = tool_button(bar, "Reload", "Reload the page", on_help_reload)
    r.Rwidget_set_enabled(APP.help_back, 0)
    r.Rwidget_set_enabled(APP.help_forward, 0)

    spacer = r.Rwidget_add_child(bar, r.Rwidget_create(r.R_WIDGET_SPACER))
    r.Rslot_set_fill(spacer, 1)

    APP.help_status = r.Rwidget_add_child(bar, r.Rlabel_create("User guide"))
    r.Rlabel_set_text_size(APP.help_status, 11)
    r.Rwidget_set_opacity(APP.help_status, 0.6)
    r.Rslot_set_valign(APP.help_status, r.R_VALIGN_CENTER)

    r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_SEPARATOR))

    # The WebView is added here the first time the pane is opened.
    APP.help_page = r.Rwidget_add_child(page, r.Rwidget_create(r.R_WIDGET_VBOX))
    r.Rslot_set_fill(APP.help_page, 1)
    return page


def context_menu(labels, actions):
    menu = r.Rwidget_create(r.R_WIDGET_MENU)
    for label, action in zip(labels, actions):
        if not label:
            r.Rmenu_add_separator(menu)
            continue
        item = r.Rmenu_add_item(menu, label)
        if action:
            r.Rwidget_set_action(item, action)
    return menu


def attach_context_menus(sidebar, application_page, panel_page, preferences_page, help_page):
    r.Rwidget_set_context_menu(
        sidebar,
        context_menu(["Application", "Instrument panel", "User guide", "", "Preferences..."],
                     ["view.application", "view.panel", "help.guide", "", "edit.preferences"]))
    r.Rwidget_set_context_menu(
        application_page,
        context_menu(["User guide", "Preferences...", "", "Copy version", "Copy configuration path"],
                     ["help.guide", "edit.preferences", "", "clipboard.version", "clipboard.config"]))
    r.Rwidget_set_context_menu(
        panel_page,
        context_menu(["Pause the simulation", "Reset the plant", "", "Copy the readings"],
                     ["plant.pause", "plant.reset", "", "clipboard.readings"]))
    r.Rwidget_set_context_menu(
        preferences_page,
        context_menu(["Save", "Reset to defaults", "", "Copy configuration path"],
                     ["preferences.save", "preferences.reset", "", "clipboard.config"]))
    r.Rwidget_set_context_menu(
        help_page,
        context_menu(["Back", "Forward", "Reload", "", "Preferences..."],
                     ["help.back", "help.forward", "help.reload", "", "edit.preferences"]))


def build_window():
    # A second window (the test builds two) must not keep the old handles.
    APP.window = r.R_INVALID_ID
    APP.switcher = r.R_INVALID_ID
    APP.status = r.R_INVALID_ID
    APP.status_right = r.R_INVALID_ID
    APP.toolbar = r.R_INVALID_ID
    APP.toolbar_panel = r.R_INVALID_ID
    APP.pause = r.R_INVALID_ID
    APP.sidebar_app = r.R_INVALID_ID
    APP.sidebar_panel = r.R_INVALID_ID
    APP.sidebar_help = r.R_INVALID_ID
    APP.app_item = r.R_INVALID_ID
    APP.panel_item = r.R_INVALID_ID
    APP.help_page = r.R_INVALID_ID
    APP.help_view = r.R_INVALID_ID
    APP.help_status = r.R_INVALID_ID
    APP.help_back = r.R_INVALID_ID
    APP.help_forward = r.R_INVALID_ID
    APP.help_reload = r.R_INVALID_ID
    APP.help_loaded = False
    APP.help_status_text = ""

    r.Rtheme_apply(r.R_THEME_LIGHT if APP.config.theme == "light" else r.R_THEME_DARK)
    APP.window = r.Rwindow_create("Raven Workbench", 1280, 820, r.R_WINDOW_DEFAULT)

    root = r.Rwidget_create(r.R_WIDGET_VBOX)
    r.Rpanel_set_spacing(root, 0)
    r.Rwindow_set_content(APP.window, root)

    # --- the menu bar ------------------------------------------------------
    bar = r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_MENU_BAR))

    file_menu = r.Rmenu_add_menu(bar, "File")
    quit_item = r.Rmenu_add_item(file_menu, "Quit")
    r.Rmenu_item_set_shortcut(quit_item, r.R_KEY_Q, r.R_MOD_CTRL)
    r.Rwidget_set_action(quit_item, "app.quit")

    edit_menu = r.Rmenu_add_menu(bar, "Edit")
    preferences_item = r.Rmenu_add_item(edit_menu, "Preferences...")
    r.Rmenu_item_set_shortcut(preferences_item, r.R_KEY_COMMA, r.R_MOD_CTRL)
    r.Rwidget_set_action(preferences_item, "edit.preferences")

    view_menu = r.Rmenu_add_menu(bar, "View")
    APP.app_item = r.Rmenu_add_item(view_menu, "Application")
    r.Rmenu_item_set_checkable(APP.app_item, 1)
    r.Rmenu_item_set_shortcut(APP.app_item, r.R_KEY_0, r.R_MOD_CTRL)
    r.Rwidget_set_action(APP.app_item, "view.application")

    APP.panel_item = r.Rmenu_add_item(view_menu, "Instrument panel")
    r.Rmenu_item_set_checkable(APP.panel_item, 1)
    r.Rmenu_item_set_shortcut(APP.panel_item, r.R_KEY_I, r.R_MOD_CTRL)
    r.Rwidget_set_action(APP.panel_item, "view.panel")

    r.Rmenu_add_separator(view_menu)
    pause_item = r.Rmenu_add_item(view_menu, "Pause the simulation")
    r.Rwidget_set_action(pause_item, "plant.pause")
    reset_item = r.Rmenu_add_item(view_menu, "Reset the plant")
    r.Rwidget_set_action(reset_item, "plant.reset")
    r.Rmenu_add_separator(view_menu)
    dark = r.Rmenu_add_item(view_menu, "Dark")
    r.Rwidget_set_action(dark, "theme.dark")
    light = r.Rmenu_add_item(view_menu, "Light")
    r.Rwidget_set_action(light, "theme.light")

    help_menu = r.Rmenu_add_menu(bar, "Help")
    guide = r.Rmenu_add_item(help_menu, "User guide")
    r.Rmenu_item_set_shortcut(guide, r.R_KEY_F1, 0)
    r.Rwidget_set_action(guide, "help.guide")
    r.Rmenu_add_separator(help_menu)
    about = r.Rmenu_add_item(help_menu, "About Workbench...")
    r.Rwidget_set_action(about, "help.about")

    # --- the toolbar, when the configuration wants one ---------------------
    if APP.config.toolbar:
        APP.toolbar = r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_HBOX))
        r.Rpanel_set_spacing(APP.toolbar, 6)
        r.Rslot_set_padding(APP.toolbar, 8, 6, 8, 6)

        APP.toolbar_panel = tool_button(APP.toolbar, "Instrument panel",
                                        "Open the instrument panel  (Ctrl+I)", on_toggle_button)
        APP.pause = tool_button(APP.toolbar, "Pause",
                                "Pause or resume the simulated plant", on_pause_button)
        tool_button(APP.toolbar, "Reset", "Reset the plant and the trend", on_reset_button)

        spacer = r.Rwidget_add_child(APP.toolbar, r.Rwidget_create(r.R_WIDGET_SPACER))
        r.Rslot_set_fill(spacer, 1)

        plant = r.Rwidget_add_child(APP.toolbar, r.Rlabel_create("SIMULATED PLANT"))
        r.Rlabel_set_text_size(plant, 11)
        r.Rwidget_set_opacity(plant, 0.55)
        r.Rslot_set_valign(plant, r.R_VALIGN_CENTER)
    else:
        # The layout still needs a pause widget: the status bar has one.
        APP.pause = r.R_INVALID_ID

    # --- the body ----------------------------------------------------------
    body = r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(body, 0)
    r.Rslot_set_fill(body, 1)

    sidebar = r.Rwidget_add_child(body, r.Rwidget_create(r.R_WIDGET_VBOX))
    r.Rpanel_set_spacing(sidebar, 6)
    r.Rslot_set_padding(sidebar, 12, 12, 12, 12)
    r.Rwidget_set_min_size(sidebar, 190, 0)

    panes = r.Rwidget_add_child(sidebar, r.Rlabel_create("PANES"))
    r.Rlabel_set_text_size(panes, 10)
    r.Rwidget_set_opacity(panes, 0.6)

    APP.sidebar_app = sidebar_button(sidebar, "Application",
                                     "Application information  (Ctrl+0)", on_sidebar_application)
    APP.sidebar_panel = sidebar_button(sidebar, "Instrument panel",
                                       "Open the instrument panel  (Ctrl+I)", on_sidebar_panel)
    APP.sidebar_help = sidebar_button(sidebar, "User guide", "This documentation  (F1)", on_sidebar_help)
    sidebar_button(sidebar, "Alarms", "A placeholder pane", on_alarms)

    sidebar_spacer = r.Rwidget_add_child(sidebar, r.Rwidget_create(r.R_WIDGET_SPACER))
    r.Rslot_set_fill(sidebar_spacer, 1)

    version = r.Rwidget_add_child(sidebar, r.Rlabel_create("Raven Workbench\n" + VERSION))
    r.Rlabel_set_text_size(version, 10)
    r.Rwidget_set_opacity(version, 0.45)

    divider = r.Rwidget_add_child(body, r.Rwidget_create(r.R_WIDGET_SEPARATOR))
    r.Rseparator_set_orientation(divider, r.R_VERTICAL)

    APP.switcher = r.Rwidget_add_child(body, r.Rwidget_create(r.R_WIDGET_SWITCHER))
    r.Rslot_set_fill(APP.switcher, 1)
    application_page = build_application_page()
    r.Rwidget_add_child(APP.switcher, application_page)

    panel_page = panel.build()
    r.Rslot_set_padding(panel_page, 16, 16, 16, 16)
    r.Rwidget_add_child(APP.switcher, panel_page)

    help_page = build_help_page()
    r.Rwidget_add_child(APP.switcher, help_page)

    APP.preferences = preferences.Preferences(
        lambda: APP.config, lambda: APP.schema, apply_live_settings, set_status)
    preferences_page = APP.preferences.build()
    r.Rslot_set_padding(preferences_page, 16, 16, 16, 16)
    r.Rwidget_add_child(APP.switcher, preferences_page)

    # A panel is transparent to the pointer by default; these areas show a
    # menu, so they must be hit. Children still take the clicks first.
    r.Rwidget_set_hit_testable(sidebar, 1)
    r.Rwidget_set_hit_testable(application_page, 1)
    r.Rwidget_set_hit_testable(panel_page, 1)
    r.Rwidget_set_hit_testable(preferences_page, 1)
    r.Rwidget_set_hit_testable(help_page, 1)

    attach_context_menus(sidebar, application_page, panel_page, preferences_page, help_page)

    # --- the status bar ----------------------------------------------------
    r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_SEPARATOR))
    status_bar = r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(status_bar, 12)
    r.Rslot_set_padding(status_bar, 12, 5, 12, 5)

    APP.status = r.Rwidget_add_child(status_bar, r.Rlabel_create(APP.config.status()))
    r.Rlabel_set_text_size(APP.status, 11)
    r.Rslot_set_fill(APP.status, 1)
    r.Rslot_set_valign(APP.status, r.R_VALIGN_CENTER)

    APP.status_right = r.Rwidget_add_child(status_bar, r.Rlabel_create(""))
    r.Rlabel_set_text_size(APP.status_right, 11)
    r.Rwidget_set_opacity(APP.status_right, 0.7)
    r.Rslot_set_valign(APP.status_right, r.R_VALIGN_CENTER)

    # --- actions -----------------------------------------------------------
    for name in ("view.application", "view.panel", "plant.pause", "plant.reset",
                 "edit.preferences", "clipboard.version", "clipboard.config",
                 "clipboard.readings", "preferences.save", "preferences.reset",
                 "help.reload", "help.back", "help.forward", "help.guide", "help.config",
                 "help.about", "theme.dark", "theme.light"):
        r.Raction_bind(name, on_action)

    # --- apply the configuration --------------------------------------------
    panel.set_alarm_limit(APP.config.alarm_limit)
    panel.set_window(APP.config.chart_seconds)
    panel.set_switch(panel.PANEL_PUMP, APP.config.pump)
    panel.set_switch(panel.PANEL_VALVE, APP.config.valve)
    panel.set_switch(panel.PANEL_INTERLOCKS, APP.config.armed)

    set_pane(APP.config.pane)
    # the first message says where the settings came from
    set_status(APP.config.status())
    update_right_status()


# ---------------------------------------------------------------------------
#  Start
# ---------------------------------------------------------------------------


def init(headless):
    desc = r.RInitDesc()
    desc.app_name = b"Raven workbench"
    if headless:
        desc.backend = r.R_BACKEND_NULL
        desc.headless = 1
    else:
        desc.backend = r.R_BACKEND_VULKAN
    if not r.Rinit_ex(desc):
        print("cannot start Raven (is a Vulkan driver installed?)")
        return False
    return True


def click(widget):
    if widget == r.R_INVALID_ID:
        return
    x, y, width, height = r.Rwidget_get_rect(widget)
    r.Rwindow_inject_mouse_move(APP.window, x + width / 2, y + height / 2)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 0)


def save_ppm(window, path):
    import ctypes
    pixels, width, height = r.Rwindow_capture(window)
    if not pixels:
        return False
    data = ctypes.string_at(pixels, width * height * 4)
    try:
        with open(path, "wb") as file:
            file.write(b"P6\n%d %d\n255\n" % (width, height))
            for i in range(width * height):
                file.write(data[i * 4:i * 4 + 3])
    except OSError:
        return False
    finally:
        r.Rpixels_free(pixels)
    return True


def run_tests():
    failures = []

    def check(condition, message):
        if not condition:
            print("  FAIL: %s" % message)
            failures.append(message)

    # --- the JSONC reader ---------------------------------------------------
    config_path = "/tmp/raven_workbench_python.jsonc"
    with open(config_path, "w", encoding="utf-8") as file:
        file.write(
            "{\n"
            "  // a hand-written configuration\n"
            '  "release": { "channel": "beta" },\n'
            '  "ui": { "theme": "light", "toolbar": false, },\n'
            "  /* the pane to open at startup */\n"
            '  "startup": { "pane": "instrument-panel", "pump": true, "valve": true },\n'
            '  "simulation": { "alarm_limit": 50, "chart_seconds": 5 },\n'
            "}\n")
    custom = settings.load(config_path)
    check(custom.loaded, "the JSONC file loads (comments and trailing commas)")
    check(custom.channel == "beta", "release.channel")
    check(custom.theme == "light", "ui.theme")
    check(not custom.toolbar, "ui.toolbar")
    check(custom.pane == 1, "startup.pane")
    check(custom.pump and custom.valve and not custom.armed, "startup switches")
    check(custom.alarm_limit == 50.0, "simulation.alarm_limit")
    check(custom.chart_seconds == 5.0, "simulation.chart_seconds")
    check(custom.settings == 8, "eight settings applied")

    with open(config_path, "w", encoding="utf-8") as file:
        file.write('{ "ui": { "theme": } }')
    broken = settings.load(config_path)
    check(not broken.loaded and broken.error, "a syntax error is reported")
    check(broken.channel == "dev", "a broken file keeps the defaults")
    check(not settings.load("/tmp/raven_workbench_absent.jsonc").error,
          "a missing file is not an error")

    # --- the default application --------------------------------------------
    APP.config = settings.load(DEFAULT_CONFIG)
    build_window()
    r.Rdraw_frame()

    check(APP.pane == PANE_APPLICATION, "the application pane is the default")
    check(r.Rswitcher_get_index(APP.switcher) == PANE_APPLICATION, "the switcher shows it")
    check(APP.toolbar != r.R_INVALID_ID, "the toolbar is shown by default")
    check(panel.PANEL.window_seconds == 10.0, "the chart window defaults to 10 s")
    check(panel.PANEL.alarm_limit == 75.0, "the alarm defaults to 75 bar")

    version = r.Rwidget_find(APP.window, "info.version")
    channel = r.Rwidget_find(APP.window, "info.channel")
    config_label = r.Rwidget_find(APP.window, "info.config")
    check(version != r.R_INVALID_ID and VERSION in r.Rlabel_get_text(version),
          "the application pane shows the version")
    check(channel != r.R_INVALID_ID and r.Rlabel_get_text(channel) == "dev",
          "the application pane shows the channel")
    check(config_label != r.R_INVALID_ID and r.Rlabel_get_text(config_label) == DEFAULT_CONFIG,
          "the application pane shows the configuration path")

    # the toolbar opens the panel, the side bar goes back
    click(APP.toolbar_panel)
    check(APP.pane == PANE_PANEL, "the toolbar opens the instrument panel")
    check(r.Rswitcher_get_index(APP.switcher) == PANE_PANEL, "the switcher follows")
    check(r.Rmenu_item_is_checked(APP.panel_item), "the View item follows the toolbar")
    click(APP.sidebar_app)
    check(APP.pane == PANE_APPLICATION, "the side bar goes back to the application")

    # the menu does the same, through the action the item fires
    r.Rmenu_item_set_checked(APP.panel_item, 1)
    r.Raction_fire("view.panel", APP.panel_item)
    check(APP.pane == PANE_PANEL, "View > Instrument panel opens it")
    r.Rmenu_item_set_checked(APP.app_item, 1)
    r.Raction_fire("view.application", APP.app_item)
    check(APP.pane == PANE_APPLICATION, "View > Application goes back")

    # the panel runs, and a hidden pane costs nothing
    set_pane(PANE_PANEL)
    r.Rdraw_frame()
    check(all(panel.paintings(which) > 0 for which in
              (panel.PAINT_METER, panel.PAINT_DIGITAL, panel.PAINT_CHART, panel.PAINT_LED)),
          "every custom widget was painted")
    for _ in range(90):
        panel.simulate(1.0 / 60.0)
    samples = panel.PANEL.sample_count
    check(samples > 0, "the panel collected samples")
    set_pane(PANE_APPLICATION)
    on_frame(1.0, None)
    check(panel.PANEL.sample_count == samples, "a hidden pane does not simulate")

    # pause and reset from the toolbar
    set_pane(PANE_PANEL)
    was_running = panel.PANEL.running
    click(APP.pause)
    check(panel.PANEL.running != was_running, "the toolbar Pause toggles the plant")
    click(APP.pause)
    r.Raction_fire("plant.reset", r.R_INVALID_ID)
    check(panel.PANEL.sample_count == 0, "Reset empties the trend")
    check(r.Rlabel_get_text(APP.status) == "Plant reset to a cold start.",
          "the status bar names the last command")

    # --- the documentation pane ---------------------------------------------
    r.Raction_fire("help.guide", r.R_INVALID_ID)
    check(APP.pane == PANE_HELP, "Help > User guide opens the documentation pane")
    check(r.Rswitcher_get_index(APP.switcher) == PANE_HELP, "the switcher shows it")
    check(APP.help_view != r.R_INVALID_ID, "the embedded browser view exists")
    check(not web.is_available(), "the test runs without a browser")
    check(web.get_error(APP.help_view) != "", "the view explains that the browser is missing")
    r.Raction_fire("help.config", r.R_INVALID_ID)
    check(APP.pane == PANE_HELP, "Help > Configuration stays in the pane")
    click(APP.sidebar_app)
    check(APP.pane == PANE_APPLICATION, "the side bar returns to the application")

    # --- the preferences editor ---------------------------------------------
    check(APP.schema.loaded, "the schema loads")
    check(len(APP.schema.groups) == 4, "four groups in the schema")
    check(APP.schema.setting_count() == 9, "nine settings in the schema")

    r.Raction_fire("edit.preferences", r.R_INVALID_ID)
    check(APP.pane == PANE_PREFERENCES, "Edit > Preferences opens the pane")
    check(r.Rswitcher_get_index(APP.switcher) == PANE_PREFERENCES, "the switcher shows it")
    check(APP.preferences.shown == 9, "every setting has a row")

    APP.preferences.set_filter("trend")
    check(APP.preferences.shown == 1, "the search filters the rows")
    APP.preferences.set_filter("")
    check(APP.preferences.shown == 9, "clearing the search shows them all")

    toolbar = r.Rwidget_find(APP.window, "setting.ui.toolbar")
    check(toolbar != r.R_INVALID_ID, "the toolbar setting has a control")
    before = APP.config.toolbar
    click(toolbar)
    check(APP.config.toolbar != before, "clicking the control changes the setting")

    # a hand-edited file keeps its comments when the settings change
    path = "/tmp/raven_workbench_python_comments.jsonc"
    with open(path, "w", encoding="utf-8") as file:
        file.write(
            "{\n"
            "  // keep this comment\n"
            '  "release": { "channel": "dev" },\n'
            '  "ui": {\n'
            '    "theme": "dark", // inline comment\n'
            '    "toolbar": true\n'
            "  },\n"
            '  "startup": { "pane": "application", "pump": false, "valve": false },\n'
            '  "simulation": { "alarm_limit": 75, "chart_seconds": 10 }\n'
            "}\n")
    edited = settings.load(path)
    check(edited.loaded, "a commented file loads")
    edited.theme = "light"
    edited.armed = True  # missing from the file: it must be added
    ok, error = settings.save(path, edited)
    check(ok, "save edits the file in place")
    with open(path, encoding="utf-8") as file:
        text = file.read()
    check("// keep this comment" in text, "comments survive a save")
    check("// inline comment" in text, "an inline comment survives a save")
    check('"theme": "light"' in text, "the value was replaced")
    check('"armed": true' in text, "a missing key was added")
    reread = settings.load(path)
    check(reread.theme == "light" and reread.armed, "the edited file reads back")
    os.remove(path)

    # save where the test may write, then read the file back
    APP.config.channel = "stable"
    APP.config.path = "/tmp/raven_workbench_python_saved.jsonc"
    APP.preferences = preferences.Preferences(
        lambda: APP.config, lambda: APP.schema, apply_live_settings, set_status)
    check(APP.preferences.save(), "Save writes the configuration")
    reloaded = settings.load(APP.config.path)
    check(reloaded.loaded, "the written file loads")
    check(reloaded.channel == "stable", "the change reached the file")
    APP.preferences.reset()
    check(APP.config.channel == "dev", "Reset restores the defaults")
    os.remove("/tmp/raven_workbench_python_saved.jsonc")

    click(APP.sidebar_app)
    check(APP.pane == PANE_APPLICATION, "the side bar leaves the preferences")

    # --- context menus ------------------------------------------------------
    set_pane(PANE_PANEL)
    r.Rdraw_frame()
    row = r.Rtheme_get_metric(r.R_METRIC_ROW_HEIGHT)

    # the side bar has a menu; its first item goes back to the application
    r.Rwindow_inject_mouse_move(APP.window, 60, 420)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 0)
    check(r.Rwindow_has_popup(APP.window), "right-clicking the side bar opens a menu")
    r.Rwindow_inject_mouse_move(APP.window, 90, 420 + 4 + row * 0.5)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 0)
    check(APP.pane == PANE_APPLICATION, "the side bar menu opens the application pane")
    check(not r.Rwindow_has_popup(APP.window), "the menu closes after the choice")

    # Escape closes a menu
    r.Rwindow_inject_mouse_move(APP.window, 60, 420)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 0)
    check(r.Rwindow_has_popup(APP.window), "the menu opens again")
    r.Rwindow_inject_key(APP.window, r.R_KEY_ESCAPE, 1, 0)
    r.Rwindow_inject_key(APP.window, r.R_KEY_ESCAPE, 0, 0)
    check(not r.Rwindow_has_popup(APP.window), "Escape closes it")

    # the instrument panel has a menu too: its first item pauses the plant
    set_pane(PANE_PANEL)
    r.Rdraw_frame()
    was_running = panel.PANEL.running
    r.Rwindow_inject_mouse_move(APP.window, 600, 500)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 0)
    check(r.Rwindow_has_popup(APP.window), "right-clicking the panel opens a menu")
    r.Rwindow_inject_mouse_move(APP.window, 630, 500 + 4 + row * 0.5)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 1)
    r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_LEFT, 0)
    check(panel.PANEL.running != was_running, "the panel menu pauses the simulation")
    panel.PANEL.running = True  # leave it running for the rest of the test

    # the clipboard actions behind the menus
    r.Raction_fire("clipboard.version", r.R_INVALID_ID)
    check(VERSION in (r.Rclipboard_get_text() or ""),
          "Copy version puts the version on the clipboard")

    # --- a configured application -------------------------------------------
    r.Rwindow_destroy(APP.window)
    APP.config = custom.copy()
    build_window()
    r.Rdraw_frame()

    check(APP.toolbar == r.R_INVALID_ID, "ui.toolbar = false hides the toolbar")
    check(APP.pane == PANE_PANEL, "startup.pane opens the instrument panel")
    check(r.Rswitcher_get_index(APP.switcher) == PANE_PANEL, "the switcher shows the panel")
    check(panel.PANEL.alarm_limit == 50.0, "simulation.alarm_limit reaches the panel")
    check(panel.PANEL.window_seconds == 5.0, "simulation.chart_seconds reaches the chart")
    check(panel.switch(panel.PANEL_PUMP) and panel.switch(panel.PANEL_VALVE),
          "startup switches are on")
    check(not r.Rtheme_is_dark(), "ui.theme = light")

    channel = r.Rwidget_find(APP.window, "info.channel")
    state = r.Rwidget_find(APP.window, "info.state")
    check(channel != r.R_INVALID_ID and r.Rlabel_get_text(channel) == "beta",
          "the application pane shows the configured channel")
    check(state != r.R_INVALID_ID and "8 settings" in r.Rlabel_get_text(state),
          "the application pane reports the loaded settings")

    # the pump from the configuration raises the pressure by itself
    start_pressure = panel.PANEL.pressure
    for _ in range(180):
        panel.simulate(1.0 / 60.0)
    check(panel.PANEL.pressure > start_pressure + 20.0, "the configured pump runs the plant")

    print("workbench (python): %d checks failed, %d samples, config %s"
          % (len(failures), panel.PANEL.sample_count, custom.status()))
    r.Rshutdown()
    print("PASS" if not failures else "FAIL")
    return 0 if not failures else 1


def config_path(arguments):
    if "--config" in arguments:
        index = arguments.index("--config")
        if index + 1 < len(arguments):
            return arguments[index + 1]
    if os.environ.get("WORKBENCH_CONFIG"):
        return os.environ["WORKBENCH_CONFIG"]
    return DEFAULT_CONFIG


def main():
    arguments = sys.argv[1:]
    test = "--test" in arguments
    capture = None
    if "--capture" in arguments:
        index = arguments.index("--capture")
        capture = arguments[index + 1] if index + 1 < len(arguments) else "shot.ppm"
    pane_override = None
    if "--pane" in arguments:
        index = arguments.index("--pane")
        pane_override = arguments[index + 1] if index + 1 < len(arguments) else None
    show_menu = "--menu" in arguments
    frames = 120
    if "--frames" in arguments:
        frames = int(arguments[arguments.index("--frames") + 1])

    if not init(test):
        return 1
    # The test does not start a browser; the view reports it, the pane still works.
    if test:
        web.set_browser_path("/nonexistent-browser")

    APP.schema = schema_module.load(SCHEMA_PATH)
    APP.config = settings.load(config_path(arguments))
    if pane_override:
        APP.config.pane = {
            "instrument-panel": PANE_PANEL,
            "user-guide": PANE_HELP,
            "preferences": PANE_PREFERENCES,
        }.get(pane_override, PANE_APPLICATION)

    if test:
        return run_tests()

    build_window()
    if capture:
        # keep the real pointer off the menus, and show the plant running
        r.Rwindow_inject_mouse_move(APP.window, 4, 700)
        r.Rdraw_frame()
        if APP.pane == PANE_PANEL:
            if not panel.switch(panel.PANEL_PUMP):
                click(panel.button(panel.PANEL_PUMP))
            if not panel.switch(panel.PANEL_VALVE):
                click(panel.button(panel.PANEL_VALVE))
            if not panel.switch(panel.PANEL_INTERLOCKS):
                click(panel.button(panel.PANEL_INTERLOCKS))
        for _ in range(frames):
            on_frame(1.0 / 60.0, None)
            r.Rwindow_request_redraw(APP.window)
            r.Rwait_frame(0.01)
        if show_menu:
            x, y = (620, 470) if APP.pane == PANE_PANEL else (60, 420)
            r.Rwindow_inject_mouse_move(APP.window, x, y)
            r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 1)
            r.Rwindow_inject_mouse_button(APP.window, r.R_MOUSE_RIGHT, 0)
            for _ in range(5):
                r.Rwindow_request_redraw(APP.window)
                r.Rwait_frame(0.02)
        saved = save_ppm(APP.window, capture)
        r.Rshutdown()
        if not saved:
            print("cannot write %s" % capture)
            return 1
        print("saved %s" % capture)
        return 0

    r.Rset_frame_callback(on_frame)
    while r.Rwait_frame(-1.0):
        pass
    r.Rshutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
