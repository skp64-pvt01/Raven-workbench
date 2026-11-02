"""The instrument panel as a reusable module (Python).

This is the pane from the C++ and Rust implementations: gauges, lamps, the strip
chart and the simulated plant, with an opaque API so the shell only decides
where the pane goes and what the menus do.

    panel.build()          the widget tree
    panel.simulate(delta)  the plant and the trend
    panel.refresh()        the labels, buttons and lamps
"""

import math

import raven as r

SAMPLES = 300  # ten seconds at 30 Hz
SAMPLE_INTERVAL = 1.0 / 30.0

PANEL_PUMP = 0
PANEL_VALVE = 1
PANEL_INTERLOCKS = 2

PAINT_METER = 0
PAINT_DIGITAL = 1
PAINT_CHART = 2
PAINT_LED = 3


class Panel:
    def __init__(self):
        self.running = True
        self.pump = False
        self.valve = False
        self.armed = False
        self.time = 0.0
        self.pressure = 2.0
        self.flow = 0.0
        self.alarm_limit = 75.0
        self.window_seconds = 10.0
        self.visible_samples = SAMPLES

        self.pressure_history = [0.0] * SAMPLES
        self.flow_history = [0.0] * SAMPLES
        self.sample_count = 0
        self.sample_head = 0
        self.last_sample = 0.0

        self.alarm_label = r.R_INVALID_ID
        self.pump_button = r.R_INVALID_ID
        self.valve_button = r.R_INVALID_ID
        self.arm_button = r.R_INVALID_ID
        self.lamp_widgets = [r.R_INVALID_ID] * 3
        self.paints = [0] * 4


PANEL = Panel()

# The lamps, by the widget that paints them: the C callback passes no Python
# object, so the panel looks the lamp up from the widget it is drawing.
LAMPS = {}


def lamp(index, on, red, green, blue):
    return {"index": index, "on": on, "color": (red, green, blue)}


POWER = lamp(0, True, 0.30, 0.90, 0.45)
LINK = lamp(1, True, 0.30, 0.65, 1.00)
ALARM = lamp(2, False, 1.00, 0.30, 0.25)


def alarm_active():
    return PANEL.armed and PANEL.pressure > PANEL.alarm_limit


# ---------------------------------------------------------------------------
#  Helpers
# ---------------------------------------------------------------------------


def role(widget, which):
    return r.Rwidget_get_color(widget, which)


def clamp01(value):
    return 0.0 if value < 0.0 else (1.0 if value > 1.0 else value)


def text_width(text, size):
    return len(text) * size * 0.60


def draw_text(painter, font, size, x, y, text, color):
    r.Rdraw_text(painter, font, size, x, y, text, *color)


def draw_text_centered(painter, font, size, center_x, y, text, color):
    draw_text(painter, font, size, center_x - text_width(text, size) / 2, y, text, color)


# ---------------------------------------------------------------------------
#  Paint callbacks
# ---------------------------------------------------------------------------


def paint_meter(widget, painter, width, height, user_data):
    PANEL.paints[PAINT_METER] += 1

    surface = role(widget, r.R_COLOR_PANEL)
    border = role(widget, r.R_COLOR_BORDER)
    text = role(widget, r.R_COLOR_TEXT)
    accent = role(widget, r.R_COLOR_ACCENT)
    danger = role(widget, r.R_COLOR_DANGER)
    track = role(widget, r.R_COLOR_TRACK)

    r.Rdraw_rect(painter, 0, 0, width, height, 14, surface[0], surface[1], surface[2], 0.85)
    r.Rdraw_rect_outline(painter, 0.5, 0.5, width - 1, height - 1, 14, 1,
                         border[0], border[1], border[2], 0.7)

    center_x = width * 0.5
    center_y = height * 0.58
    radius = min(width, height) * 0.36
    start = 2.35619449   # 135 degrees: bottom left
    sweep = 4.71238898   # 270 degrees, through the top

    for i in range(11):
        angle = start + sweep * i / 10.0
        major = i % 5 == 0
        inner = radius * (0.80 if major else 0.88)
        r.Rdraw_line(
            painter,
            center_x + math.cos(angle) * inner, center_y + math.sin(angle) * inner,
            center_x + math.cos(angle) * radius, center_y + math.sin(angle) * radius,
            2.0 if major else 1.0,
            text[0], text[1], text[2], 0.9 if major else 0.4,
        )

    value = clamp01(PANEL.pressure / 100.0)
    for i in range(60):
        a0 = start + sweep * i / 60.0
        a1 = start + sweep * (i + 1) / 60.0
        lit = (i + 1) / 60.0 <= value
        color = accent if lit else track
        r.Rdraw_line(
            painter,
            center_x + math.cos(a0) * radius * 0.92, center_y + math.sin(a0) * radius * 0.92,
            center_x + math.cos(a1) * radius * 0.92, center_y + math.sin(a1) * radius * 0.92,
            4.0, color[0], color[1], color[2], color[3] * (0.9 if lit else 0.5),
        )

    angle = start + sweep * value
    needle = danger if PANEL.pressure > PANEL.alarm_limit else text
    r.Rdraw_line(painter, center_x, center_y,
                 center_x + math.cos(angle) * radius * 0.78,
                 center_y + math.sin(angle) * radius * 0.78,
                 3.0, needle[0], needle[1], needle[2], 1.0)
    r.Rdraw_circle(painter, center_x, center_y, 6, needle[0], needle[1], needle[2], 1.0)
    r.Rdraw_circle(painter, center_x, center_y, 2.5, surface[0], surface[1], surface[2], 1.0)

    draw_text(painter, r.R_INVALID_ID, 12, 14, 10, "PRESSURE  P-101", text)
    draw_text_centered(painter, r.Rfont_get_monospace(), 26, center_x, height - 40,
                       "%.1f bar" % PANEL.pressure, accent)


def paint_digital(widget, painter, width, height, user_data):
    PANEL.paints[PAINT_DIGITAL] += 1

    surface = role(widget, r.R_COLOR_PANEL)
    border = role(widget, r.R_COLOR_BORDER)
    text = role(widget, r.R_COLOR_TEXT)
    accent = role(widget, r.R_COLOR_ACCENT)

    r.Rdraw_rect(painter, 0, 0, width, height, 14, surface[0], surface[1], surface[2], 0.85)
    r.Rdraw_rect_outline(painter, 0.5, 0.5, width - 1, height - 1, 14, 1,
                         border[0], border[1], border[2], 0.7)

    r.Rdraw_rect(painter, 14, 34, width - 28, height - 62, 8, 0.05, 0.07, 0.09, 0.85)
    r.Rdraw_rect_outline(painter, 14.5, 34.5, width - 29, height - 63, 8, 1,
                         border[0], border[1], border[2], 0.5)

    size = height * 0.26
    draw_text_centered(painter, r.Rfont_get_monospace(), size, width * 0.5,
                       34 + (height - 62 - size) * 0.45, "%5.1f" % PANEL.flow, accent)

    draw_text(painter, r.R_INVALID_ID, 12, 14, 10, "FLOW  F-201", text)
    draw_text(painter, r.R_INVALID_ID, 12, width - 58, 10, "L/min", text)

    r.Rdraw_rect(painter, 20, height - 18, width - 40, 3, 1.5,
                 border[0], border[1], border[2], 0.35)
    r.Rdraw_rect(painter, 20, height - 18, (width - 40) * clamp01(PANEL.flow / 100.0), 3, 1.5,
                 accent[0], accent[1], accent[2], 0.9)


def paint_led(widget, painter, width, height, user_data):
    lamp_value = LAMPS[widget]
    PANEL.paints[PAINT_LED] += 1

    center_x = width * 0.5
    center_y = height * 0.5
    radius = min(width, height) * 0.30
    strength = 1.0 if lamp_value["on"] else 0.0
    red, green, blue = lamp_value["color"]

    for i in range(3):
        r.Rdraw_circle(painter, center_x, center_y, radius * (1.4 + 0.35 * i),
                       red, green, blue, (0.10 - 0.03 * i) * strength)

    r.Rdraw_circle(painter, center_x, center_y, radius,
                   red * (0.35 + 0.65 * strength),
                   green * (0.35 + 0.65 * strength),
                   blue * (0.35 + 0.65 * strength), 0.95)

    r.Rdraw_circle(painter, center_x - radius * 0.25, center_y - radius * 0.30, radius * 0.28,
                   1.0, 1.0, 1.0, 0.18 + 0.22 * strength)


def paint_chart(widget, painter, width, height, user_data):
    PANEL.paints[PAINT_CHART] += 1

    surface = role(widget, r.R_COLOR_PANEL)
    border = role(widget, r.R_COLOR_BORDER)
    text = role(widget, r.R_COLOR_TEXT)
    accent = role(widget, r.R_COLOR_ACCENT)
    success = role(widget, r.R_COLOR_SUCCESS)

    r.Rdraw_rect(painter, 0, 0, width, height, 14, surface[0], surface[1], surface[2], 0.85)
    r.Rdraw_rect_outline(painter, 0.5, 0.5, width - 1, height - 1, 14, 1,
                         border[0], border[1], border[2], 0.7)

    left, right, top, bottom = 52.0, 18.0, 34.0, 22.0
    plot_width = width - left - right
    plot_height = height - top - bottom

    draw_text(painter, r.R_INVALID_ID, 12, 14, 10, "TREND  %.0f s" % PANEL.window_seconds, text)
    r.Rdraw_rect(painter, width - 128, 14, 9, 9, 2, accent[0], accent[1], accent[2], 0.9)
    draw_text(painter, r.R_INVALID_ID, 11, width - 114, 11, "pressure", text)
    r.Rdraw_rect(painter, width - 52, 14, 9, 9, 2, success[0], success[1], success[2], 0.9)
    draw_text(painter, r.R_INVALID_ID, 11, width - 38, 11, "flow", text)

    for i in range(5):
        y = top + plot_height * i / 4.0
        r.Rdraw_line(painter, left, y, left + plot_width, y, 1,
                     border[0], border[1], border[2], 0.6 if i == 4 else 0.22)
        draw_text(painter, r.Rfont_get_monospace(), 10, 16, y - 6, "%d" % (100 - 25 * i), text)
    for i in range(11):
        x = left + plot_width * i / 10.0
        r.Rdraw_line(painter, x, top, x, top + plot_height, 1,
                     border[0], border[1], border[2], 0.12)

    if PANEL.sample_count < 2:
        return

    visible = min(PANEL.visible_samples, PANEL.sample_count)
    step_x = plot_width / (PANEL.visible_samples - 1)
    previous_pressure = (0.0, 0.0)
    previous_flow = (0.0, 0.0)

    for i in range(visible):
        index = (PANEL.sample_head - visible + i + SAMPLES) % SAMPLES
        x = left + plot_width - step_x * (visible - 1 - i)
        pressure_y = top + plot_height * (1.0 - clamp01(PANEL.pressure_history[index] / 100.0))
        flow_y = top + plot_height * (1.0 - clamp01(PANEL.flow_history[index] / 100.0))

        if i > 0:
            r.Rdraw_line(painter, previous_pressure[0], previous_pressure[1], x, pressure_y, 1.8,
                         accent[0], accent[1], accent[2], 0.95)
            r.Rdraw_line(painter, previous_flow[0], previous_flow[1], x, flow_y, 1.8,
                         success[0], success[1], success[2], 0.95)
        previous_pressure = (x, pressure_y)
        previous_flow = (x, flow_y)

    if PANEL.armed:
        y = top + plot_height * (1.0 - PANEL.alarm_limit / 100.0)
        r.Rdraw_line(painter, left, y, left + plot_width, y, 1.5, 0.95, 0.35, 0.30, 0.5)
        draw_text(painter, r.R_INVALID_ID, 10, left + 6, y - 14,
                  "alarm %.0f" % PANEL.alarm_limit, text)


# ---------------------------------------------------------------------------
#  Controls and layout
# ---------------------------------------------------------------------------


def sync_controls():
    r.Rbutton_set_text(PANEL.pump_button, "PUMP  ON" if PANEL.pump else "PUMP  OFF")
    r.Rbutton_set_style(PANEL.pump_button, r.R_BUTTON_PRIMARY if PANEL.pump else r.R_BUTTON_DEFAULT)
    r.Rbutton_set_text(PANEL.valve_button, "VALVE  OPEN" if PANEL.valve else "VALVE  SHUT")
    r.Rbutton_set_style(PANEL.valve_button, r.R_BUTTON_PRIMARY if PANEL.valve else r.R_BUTTON_DEFAULT)
    r.Rbutton_set_text(PANEL.arm_button, "INTERLOCKS  ARMED" if PANEL.armed else "INTERLOCKS  OFF")
    r.Rbutton_set_style(PANEL.arm_button, r.R_BUTTON_PRIMARY if PANEL.armed else r.R_BUTTON_DEFAULT)

    if alarm_active():
        message, color = "HIGH PRESSURE", (1.0, 0.35, 0.30, 1.0)
    elif not PANEL.armed:
        message, color = "interlocks off", (0.75, 0.75, 0.75, 0.8)
    else:
        message, color = "interlocks armed", (0.45, 0.85, 0.50, 0.9)
    r.Rlabel_set_text(PANEL.alarm_label, message)
    r.Rlabel_set_color(PANEL.alarm_label, *color)
    ALARM["on"] = alarm_active()


def on_pump(button, user_data):
    PANEL.pump = not PANEL.pump
    sync_controls()


def on_valve(button, user_data):
    PANEL.valve = not PANEL.valve
    sync_controls()


def on_arm(button, user_data):
    PANEL.armed = not PANEL.armed
    sync_controls()


def tile():
    column = r.Rwidget_create(r.R_WIDGET_VBOX)
    r.Rpanel_set_spacing(column, 8)
    r.Rslot_set_padding(column, 12, 12, 12, 12)
    return column


def custom_panel(paint, min_width, min_height):
    widget = r.Rwidget_create(r.R_WIDGET_CUSTOM)
    r.Rcustom_set_paint_callback(widget, paint)
    r.Rwidget_set_min_size(widget, min_width, min_height)
    r.Rwidget_set_hit_testable(widget, 0)
    return widget


def build():
    """Builds the instruments; returns the root widget of the pane."""
    root = r.Rwidget_create(r.R_WIDGET_VBOX)
    r.Rpanel_set_spacing(root, 12)

    row = r.Rwidget_add_child(root, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(row, 12)

    gauge_tile = r.Rwidget_add_child(row, tile())
    r.Rslot_set_fill(gauge_tile, 1)
    gauge = r.Rwidget_add_child(gauge_tile, custom_panel(paint_meter, 240, 240))
    r.Rslot_set_fill(gauge, 1)

    flow_tile = r.Rwidget_add_child(row, tile())
    r.Rslot_set_fill(flow_tile, 1)
    flow = r.Rwidget_add_child(flow_tile, custom_panel(paint_digital, 240, 150))
    r.Rslot_set_fill(flow, 1)

    control_tile = r.Rwidget_add_child(row, tile())
    r.Rslot_set_fill(control_tile, 1)

    leds = r.Rwidget_add_child(control_tile, r.Rwidget_create(r.R_WIDGET_HBOX))
    r.Rpanel_set_spacing(leds, 10)
    r.Rslot_set_halign(leds, r.R_HALIGN_CENTER)

    for index, (lamp_value, name) in enumerate(((POWER, "POWER"), (LINK, "LINK"), (ALARM, "ALARM"))):
        column = r.Rwidget_add_child(leds, r.Rwidget_create(r.R_WIDGET_VBOX))
        r.Rpanel_set_spacing(column, 2)
        widget = r.Rwidget_add_child(column, custom_panel(paint_led, 46, 46))
        LAMPS[widget] = lamp_value
        PANEL.lamp_widgets[index] = widget
        r.Rwidget_set_size(widget, 46, 46)
        label = r.Rwidget_add_child(column, r.Rlabel_create(name))
        r.Rlabel_set_text_size(label, 10)
        r.Rslot_set_halign(label, r.R_HALIGN_CENTER)

    PANEL.alarm_label = r.Rwidget_add_child(control_tile, r.Rlabel_create("interlocks off"))
    r.Rlabel_set_text_size(PANEL.alarm_label, 12)
    r.Rslot_set_halign(PANEL.alarm_label, r.R_HALIGN_CENTER)

    PANEL.pump_button = r.Rwidget_add_child(control_tile, r.Rbutton_create("PUMP  OFF"))
    r.Rbutton_set_clicked_callback(PANEL.pump_button, on_pump)
    PANEL.valve_button = r.Rwidget_add_child(control_tile, r.Rbutton_create("VALVE  SHUT"))
    r.Rbutton_set_clicked_callback(PANEL.valve_button, on_valve)
    PANEL.arm_button = r.Rwidget_add_child(control_tile, r.Rbutton_create("INTERLOCKS  OFF"))
    r.Rbutton_set_clicked_callback(PANEL.arm_button, on_arm)

    spacer = r.Rwidget_add_child(control_tile, r.Rwidget_create(r.R_WIDGET_SPACER))
    r.Rslot_set_fill(spacer, 1)

    chart_tile = r.Rwidget_add_child(root, tile())
    r.Rslot_set_fill(chart_tile, 1)
    chart = r.Rwidget_add_child(chart_tile, custom_panel(paint_chart, 480, 180))
    r.Rslot_set_fill(chart, 1)

    sync_controls()
    return root


# ---------------------------------------------------------------------------
#  Public API
# ---------------------------------------------------------------------------


def simulate(delta):
    if not PANEL.running or delta <= 0.0:
        return
    PANEL.time += delta
    t = PANEL.time

    pressure_target = 68.0 if PANEL.pump else 2.0
    if PANEL.pump:
        pressure_target += 2.5 * math.sin(t * 1.7)

    flow_target = (44.0 if PANEL.pump else 12.0) if PANEL.valve else 0.0
    if PANEL.valve:
        flow_target += 3.0 * math.sin(t * 3.1)

    PANEL.pressure += (pressure_target - PANEL.pressure) * (1.0 - math.exp(-delta / 0.6))
    PANEL.flow += (flow_target - PANEL.flow) * (1.0 - math.exp(-delta / 0.35))
    PANEL.pressure = max(PANEL.pressure, 0.0)
    PANEL.flow = max(PANEL.flow, 0.0)

    if PANEL.time - PANEL.last_sample >= SAMPLE_INTERVAL:
        PANEL.last_sample = PANEL.time
        PANEL.pressure_history[PANEL.sample_head] = PANEL.pressure
        PANEL.flow_history[PANEL.sample_head] = PANEL.flow
        PANEL.sample_head = (PANEL.sample_head + 1) % SAMPLES
        PANEL.sample_count = min(PANEL.sample_count + 1, SAMPLES)


def refresh():
    sync_controls()


def reset():
    PANEL.time = 0.0
    PANEL.last_sample = 0.0
    PANEL.pressure = 2.0
    PANEL.flow = 0.0
    PANEL.sample_count = 0
    PANEL.sample_head = 0
    PANEL.pressure_history = [0.0] * SAMPLES
    PANEL.flow_history = [0.0] * SAMPLES
    sync_controls()


def set_alarm_limit(bar):
    PANEL.alarm_limit = min(100.0, max(10.0, bar))
    ALARM["on"] = alarm_active()


def set_window(seconds):
    PANEL.window_seconds = min(10.0, max(1.0, seconds))
    samples = int(PANEL.window_seconds * 30.0 + 0.5)
    PANEL.visible_samples = min(SAMPLES, max(30, samples))


def set_switch(which, on):
    if which == PANEL_PUMP:
        PANEL.pump = on
    elif which == PANEL_VALVE:
        PANEL.valve = on
    elif which == PANEL_INTERLOCKS:
        PANEL.armed = on
    sync_controls()


def switch(which):
    return {
        PANEL_PUMP: PANEL.pump,
        PANEL_VALVE: PANEL.valve,
        PANEL_INTERLOCKS: PANEL.armed,
    }.get(which, False)


def button(which):
    return {
        PANEL_PUMP: PANEL.pump_button,
        PANEL_VALVE: PANEL.valve_button,
        PANEL_INTERLOCKS: PANEL.arm_button,
    }.get(which, r.R_INVALID_ID)


def paintings(which):
    return PANEL.paints[which]
