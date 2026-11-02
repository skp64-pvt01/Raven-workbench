/**
 * The instrument panel as a reusable module - see panel.h.
 *
 * Everything the panel draws is here: the two meters, the lamps, the strip
 * chart and the simulated plant. The shell (the example's main, or the
 * workbench) only decides where the pane goes and what the menus do.
 */

#include "panel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static const int SAMPLES = 300;              /* ten seconds at 30 Hz */
static const double SAMPLE_INTERVAL = 1.0 / 30.0;

struct Led {
	Panel* owner;
	bool on;
	float r, g, b;
};

struct Panel {
	/* the plant */
	bool running;
	bool pump;
	bool valve;
	bool armed;
	double time;
	float pressure;
	float flow;
	double alarm_limit;
	double window_seconds;
	int visible_samples;

	/* the trend */
	float pressure_history[SAMPLES];
	float flow_history[SAMPLES];
	int sample_count;
	int sample_head;
	double last_sample;

	/* widgets */
	Led power;
	Led link;
	Led alarm;
	Rid alarm_label;
	Rid pump_button;
	Rid valve_button;
	Rid arm_button;

	/* what the tests count */
	int paints[4];
};

/* ---------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------- */

namespace {

struct Color {
	float r, g, b, a;
};

Color role(Rid widget, RColorRole which) {
	Color color{0, 0, 0, 0};
	Rwidget_get_color(widget, which, &color.r, &color.g, &color.b, &color.a);
	return color;
}

float clamp01(float value) {
	return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/* Raven has no text measuring call here, and the panel only centres short
 * strings. */
float text_width(const char* text, float size) {
	return (float)std::strlen(text) * size * 0.60f;
}

void draw_text(Rid font, RPainter* painter, float size, float x, float y,
	const char* text, Color color) {
	Rdraw_text(painter, font, size, x, y, text, color.r, color.g, color.b, color.a);
}

void draw_text_centered(Rid font, RPainter* painter, float size, float center_x,
	float y, const char* text, Color color) {
	draw_text(font, painter, size, center_x - text_width(text, size) * 0.5f, y, text, color);
}

} /* namespace */

/* ---------------------------------------------------------------------------
 *  Paint: analog meter
 * ------------------------------------------------------------------------- */

static void paint_meter(Rid widget, RPainter* painter, float width, float height, void* user_data) {
	Panel* panel = static_cast<Panel*>(user_data);
	panel->paints[PANEL_PAINT_METER]++;

	Color surface = role(widget, R_COLOR_PANEL);
	Color border = role(widget, R_COLOR_BORDER);
	Color text = role(widget, R_COLOR_TEXT);
	Color accent = role(widget, R_COLOR_ACCENT);
	Color danger = role(widget, R_COLOR_DANGER);
	Color track = role(widget, R_COLOR_TRACK);

	Rdraw_rect(painter, 0, 0, width, height, 14.0f, surface.r, surface.g, surface.b, 0.85f);
	Rdraw_rect_outline(painter, 0.5f, 0.5f, width - 1.0f, height - 1.0f, 14.0f, 1.0f,
		border.r, border.g, border.b, 0.7f);

	const float center_x = width * 0.5f;
	const float center_y = height * 0.58f;
	const float radius = (width < height ? width : height) * 0.36f;
	const float start = 2.35619449f;              /* 135 degrees: bottom left */
	const float sweep = 4.71238898f;              /* 270 degrees, through the top */

	for (int i = 0; i <= 10; i++) {
		const float angle = start + sweep * (float)i / 10.0f;
		const bool major = (i % 5) == 0;
		const float inner = radius * (major ? 0.80f : 0.88f);
		Rdraw_line(painter,
			center_x + std::cos(angle) * inner, center_y + std::sin(angle) * inner,
			center_x + std::cos(angle) * radius, center_y + std::sin(angle) * radius,
			major ? 2.0f : 1.0f, text.r, text.g, text.b, major ? 0.9f : 0.4f);
	}

	const float value = clamp01(panel->pressure / 100.0f);
	for (int i = 0; i < 60; i++) {
		const float a0 = start + sweep * (float)i / 60.0f;
		const float a1 = start + sweep * (float)(i + 1) / 60.0f;
		const bool lit = ((float)(i + 1) / 60.0f) <= value;
		const Color color = lit ? accent : track;
		Rdraw_line(painter,
			center_x + std::cos(a0) * radius * 0.92f, center_y + std::sin(a0) * radius * 0.92f,
			center_x + std::cos(a1) * radius * 0.92f, center_y + std::sin(a1) * radius * 0.92f,
			4.0f, color.r, color.g, color.b, color.a * (lit ? 0.9f : 0.5f));
	}

	const float angle = start + sweep * value;
	const Color needle = panel->pressure > (float)panel->alarm_limit ? danger : text;
	Rdraw_line(painter, center_x, center_y,
		center_x + std::cos(angle) * radius * 0.78f, center_y + std::sin(angle) * radius * 0.78f,
		3.0f, needle.r, needle.g, needle.b, 1.0f);
	Rdraw_circle(painter, center_x, center_y, 6.0f, needle.r, needle.g, needle.b, 1.0f);
	Rdraw_circle(painter, center_x, center_y, 2.5f, surface.r, surface.g, surface.b, 1.0f);

	char amount[32];
	std::snprintf(amount, sizeof(amount), "%.1f bar", (double)panel->pressure);
	draw_text(R_INVALID_ID, painter, 12.0f, 14.0f, 10.0f, "PRESSURE  P-101", text);
	draw_text_centered(Rfont_get_monospace(), painter, 26.0f, center_x, height - 40.0f, amount, accent);
}

/* ---------------------------------------------------------------------------
 *  Paint: digital meter
 * ------------------------------------------------------------------------- */

static void paint_digital(Rid widget, RPainter* painter, float width, float height, void* user_data) {
	Panel* panel = static_cast<Panel*>(user_data);
	panel->paints[PANEL_PAINT_DIGITAL]++;

	Color surface = role(widget, R_COLOR_PANEL);
	Color border = role(widget, R_COLOR_BORDER);
	Color text = role(widget, R_COLOR_TEXT);
	Color accent = role(widget, R_COLOR_ACCENT);

	Rdraw_rect(painter, 0, 0, width, height, 14.0f, surface.r, surface.g, surface.b, 0.85f);
	Rdraw_rect_outline(painter, 0.5f, 0.5f, width - 1.0f, height - 1.0f, 14.0f, 1.0f,
		border.r, border.g, border.b, 0.7f);

	Rdraw_rect(painter, 14.0f, 34.0f, width - 28.0f, height - 62.0f, 8.0f, 0.05f, 0.07f, 0.09f, 0.85f);
	Rdraw_rect_outline(painter, 14.5f, 34.5f, width - 29.0f, height - 63.0f, 8.0f, 1.0f,
		border.r, border.g, border.b, 0.5f);

	char amount[32];
	std::snprintf(amount, sizeof(amount), "%5.1f", (double)panel->flow);
	const float size = height * 0.26f;
	draw_text_centered(Rfont_get_monospace(), painter, size, width * 0.5f,
		34.0f + (height - 62.0f - size) * 0.45f, amount, accent);

	draw_text(R_INVALID_ID, painter, 12.0f, 14.0f, 10.0f, "FLOW  F-201", text);
	draw_text(R_INVALID_ID, painter, 12.0f, width - 58.0f, 10.0f, "L/min", text);

	Rdraw_rect(painter, 20.0f, height - 18.0f, width - 40.0f, 3.0f, 1.5f,
		border.r, border.g, border.b, 0.35f);
	Rdraw_rect(painter, 20.0f, height - 18.0f, (width - 40.0f) * clamp01(panel->flow / 100.0f),
		3.0f, 1.5f, accent.r, accent.g, accent.b, 0.9f);
}

/* ---------------------------------------------------------------------------
 *  Paint: lamp
 * ------------------------------------------------------------------------- */

static void paint_led(Rid widget, RPainter* painter, float width, float height, void* user_data) {
	Led* led = static_cast<Led*>(user_data);
	led->owner->paints[PANEL_PAINT_LED]++;

	const float center_x = width * 0.5f;
	const float center_y = height * 0.5f;
	const float radius = (width < height ? width : height) * 0.30f;
	const float strength = led->on ? 1.0f : 0.0f;

	for (int i = 0; i < 3; i++) {
		Rdraw_circle(painter, center_x, center_y, radius * (1.4f + 0.35f * (float)i),
			led->r, led->g, led->b, (0.10f - 0.03f * (float)i) * strength);
	}
	Rdraw_circle(painter, center_x, center_y, radius,
		led->r * (0.35f + 0.65f * strength), led->g * (0.35f + 0.65f * strength),
		led->b * (0.35f + 0.65f * strength), 0.95f);
	Rdraw_circle(painter, center_x - radius * 0.25f, center_y - radius * 0.30f, radius * 0.28f,
		1.0f, 1.0f, 1.0f, 0.18f + 0.22f * strength);
	(void)widget;
}

/* ---------------------------------------------------------------------------
 *  Paint: real-time strip chart
 * ------------------------------------------------------------------------- */

static void paint_chart(Rid widget, RPainter* painter, float width, float height, void* user_data) {
	Panel* panel = static_cast<Panel*>(user_data);
	panel->paints[PANEL_PAINT_CHART]++;

	Color surface = role(widget, R_COLOR_PANEL);
	Color border = role(widget, R_COLOR_BORDER);
	Color text = role(widget, R_COLOR_TEXT);
	Color accent = role(widget, R_COLOR_ACCENT);
	Color success = role(widget, R_COLOR_SUCCESS);

	Rdraw_rect(painter, 0, 0, width, height, 14.0f, surface.r, surface.g, surface.b, 0.85f);
	Rdraw_rect_outline(painter, 0.5f, 0.5f, width - 1.0f, height - 1.0f, 14.0f, 1.0f,
		border.r, border.g, border.b, 0.7f);

	const float left = 52.0f, right = 18.0f, top = 34.0f, bottom = 22.0f;
	const float plot_width = width - left - right;
	const float plot_height = height - top - bottom;

	char title[32];
	std::snprintf(title, sizeof(title), "TREND  %.0f s", panel->window_seconds);
	draw_text(R_INVALID_ID, painter, 12.0f, 14.0f, 10.0f, title, text);
	Rdraw_rect(painter, width - 128.0f, 14.0f, 9.0f, 9.0f, 2.0f, accent.r, accent.g, accent.b, 0.9f);
	draw_text(R_INVALID_ID, painter, 11.0f, width - 114.0f, 11.0f, "pressure", text);
	Rdraw_rect(painter, width - 52.0f, 14.0f, 9.0f, 9.0f, 2.0f, success.r, success.g, success.b, 0.9f);
	draw_text(R_INVALID_ID, painter, 11.0f, width - 38.0f, 11.0f, "flow", text);

	for (int i = 0; i <= 4; i++) {
		const float y = top + plot_height * (float)i / 4.0f;
		Rdraw_line(painter, left, y, left + plot_width, y, 1.0f,
			border.r, border.g, border.b, i == 4 ? 0.6f : 0.22f);
		char label[8];
		std::snprintf(label, sizeof(label), "%d", 100 - 25 * i);
		draw_text(Rfont_get_monospace(), painter, 10.0f, 16.0f, y - 6.0f, label, text);
	}
	for (int i = 0; i <= 10; i++) {
		const float x = left + plot_width * (float)i / 10.0f;
		Rdraw_line(painter, x, top, x, top + plot_height, 1.0f, border.r, border.g, border.b, 0.12f);
	}

	if (panel->sample_count < 2) return;

	const int visible = panel->visible_samples < panel->sample_count ? panel->visible_samples : panel->sample_count;
	const float step_x = plot_width / (float)(panel->visible_samples - 1);
	float previous_pressure_x = 0, previous_pressure_y = 0;
	float previous_flow_x = 0, previous_flow_y = 0;

	for (int i = 0; i < visible; i++) {
		const int index = (panel->sample_head - visible + i + SAMPLES) % SAMPLES;
		const float x = left + plot_width - step_x * (float)(visible - 1 - i);
		const float pressure_y = top + plot_height * (1.0f - clamp01(panel->pressure_history[index] / 100.0f));
		const float flow_y = top + plot_height * (1.0f - clamp01(panel->flow_history[index] / 100.0f));

		if (i > 0) {
			Rdraw_line(painter, previous_pressure_x, previous_pressure_y, x, pressure_y, 1.8f,
				accent.r, accent.g, accent.b, 0.95f);
			Rdraw_line(painter, previous_flow_x, previous_flow_y, x, flow_y, 1.8f,
				success.r, success.g, success.b, 0.95f);
		}
		previous_pressure_x = x;
		previous_pressure_y = pressure_y;
		previous_flow_x = x;
		previous_flow_y = flow_y;
	}

	if (panel->armed) {
		char label[32];
		std::snprintf(label, sizeof(label), "alarm %.0f", panel->alarm_limit);
		const float y = top + plot_height * (1.0f - (float)(panel->alarm_limit / 100.0));
		Rdraw_line(painter, left, y, left + plot_width, y, 1.5f, 0.95f, 0.35f, 0.30f, 0.5f);
		draw_text(R_INVALID_ID, painter, 10.0f, left + 6.0f, y - 14.0f, label, text);
	}
}

/* ---------------------------------------------------------------------------
 *  Controls
 * ------------------------------------------------------------------------- */

static void sync_controls(Panel* panel) {
	Rbutton_set_text(panel->pump_button, panel->pump ? "PUMP  ON" : "PUMP  OFF");
	Rbutton_set_style(panel->pump_button, panel->pump ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	Rbutton_set_text(panel->valve_button, panel->valve ? "VALVE  OPEN" : "VALVE  SHUT");
	Rbutton_set_style(panel->valve_button, panel->valve ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);
	Rbutton_set_text(panel->arm_button, panel->armed ? "INTERLOCKS  ARMED" : "INTERLOCKS  OFF");
	Rbutton_set_style(panel->arm_button, panel->armed ? R_BUTTON_PRIMARY : R_BUTTON_DEFAULT);

	const char* message;
	Color color;
	if (panel->armed && panel->pressure > (float)panel->alarm_limit) {
		message = "HIGH PRESSURE";
		color = {1.0f, 0.35f, 0.30f, 1.0f};
	} else if (!panel->armed) {
		message = "interlocks off";
		color = {0.75f, 0.75f, 0.75f, 0.8f};
	} else {
		message = "interlocks armed";
		color = {0.45f, 0.85f, 0.50f, 0.9f};
	}
	Rlabel_set_text(panel->alarm_label, message);
	Rlabel_set_color(panel->alarm_label, color.r, color.g, color.b, color.a);
}

static void on_pump(Rid button, void* user_data) {
	(void)button;
	Panel* panel = static_cast<Panel*>(user_data);
	panel->pump = !panel->pump;
	sync_controls(panel);
}

static void on_valve(Rid button, void* user_data) {
	(void)button;
	Panel* panel = static_cast<Panel*>(user_data);
	panel->valve = !panel->valve;
	sync_controls(panel);
}

static void on_arm(Rid button, void* user_data) {
	(void)button;
	Panel* panel = static_cast<Panel*>(user_data);
	panel->armed = !panel->armed;
	sync_controls(panel);
}

static Rid tile() {
	Rid column = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(column, 8.0f);
	Rslot_set_padding(column, 12.0f, 12.0f, 12.0f, 12.0f);
	return column;
}

static Rid custom_panel(RPaintCallback paint, float min_width, float min_height, void* user_data) {
	Rid widget = Rwidget_create(R_WIDGET_CUSTOM);
	Rcustom_set_paint_callback(widget, paint, user_data);
	Rwidget_set_min_size(widget, min_width, min_height);
	Rwidget_set_hit_testable(widget, R_FALSE);
	return widget;
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

Panel* panel_create() {
	Panel* panel = new Panel();
	std::memset(panel, 0, sizeof(Panel));
	panel->running = true;
	panel->pressure = 2.0f;
	panel->alarm_limit = 75.0;
	panel->window_seconds = 10.0;
	panel->visible_samples = SAMPLES;
	panel->power = {panel, true, 0.30f, 0.90f, 0.45f};
	panel->link = {panel, true, 0.30f, 0.65f, 1.00f};
	panel->alarm = {panel, false, 1.00f, 0.30f, 0.25f};
	return panel;
}

void panel_destroy(Panel* panel) {
	delete panel;
}

Rid panel_build(Panel* panel) {
	Rid root = Rwidget_create(R_WIDGET_VBOX);
	Rpanel_set_spacing(root, 12.0f);

	Rid row = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(row, 12.0f);

	Rid gauge_tile = Rwidget_add_child(row, tile());
	Rslot_set_fill(gauge_tile, 1.0f);
	Rid gauge = Rwidget_add_child(gauge_tile, custom_panel(paint_meter, 240.0f, 240.0f, panel));
	Rslot_set_fill(gauge, 1.0f);

	Rid flow_tile = Rwidget_add_child(row, tile());
	Rslot_set_fill(flow_tile, 1.0f);
	Rid flow = Rwidget_add_child(flow_tile, custom_panel(paint_digital, 240.0f, 150.0f, panel));
	Rslot_set_fill(flow, 1.0f);

	Rid control_tile = Rwidget_add_child(row, tile());
	Rslot_set_fill(control_tile, 1.0f);

	Rid leds = Rwidget_add_child(control_tile, Rwidget_create(R_WIDGET_HBOX));
	Rpanel_set_spacing(leds, 10.0f);
	Rslot_set_halign(leds, R_HALIGN_CENTER);

	struct Lamp {
		Led* led;
		const char* name;
	} lamps[] = {{&panel->power, "POWER"}, {&panel->link, "LINK"}, {&panel->alarm, "ALARM"}};
	for (auto& lamp : lamps) {
		Rid column = Rwidget_add_child(leds, Rwidget_create(R_WIDGET_VBOX));
		Rpanel_set_spacing(column, 2.0f);
		Rid led = Rwidget_add_child(column, custom_panel(paint_led, 46.0f, 46.0f, lamp.led));
		Rwidget_set_size(led, 46.0f, 46.0f);
		Rid name = Rwidget_add_child(column, Rlabel_create(lamp.name));
		Rlabel_set_text_size(name, 10.0f);
		Rslot_set_halign(name, R_HALIGN_CENTER);
	}

	panel->alarm_label = Rwidget_add_child(control_tile, Rlabel_create("interlocks off"));
	Rlabel_set_text_size(panel->alarm_label, 12.0f);
	Rslot_set_halign(panel->alarm_label, R_HALIGN_CENTER);

	panel->pump_button = Rwidget_add_child(control_tile, Rbutton_create("PUMP  OFF"));
	Rbutton_set_clicked_callback(panel->pump_button, on_pump, panel);
	panel->valve_button = Rwidget_add_child(control_tile, Rbutton_create("VALVE  SHUT"));
	Rbutton_set_clicked_callback(panel->valve_button, on_valve, panel);
	panel->arm_button = Rwidget_add_child(control_tile, Rbutton_create("INTERLOCKS  OFF"));
	Rbutton_set_clicked_callback(panel->arm_button, on_arm, panel);

	Rid spacer = Rwidget_add_child(control_tile, Rwidget_create(R_WIDGET_SPACER));
	Rslot_set_fill(spacer, 1.0f);

	Rid chart_tile = Rwidget_add_child(root, tile());
	Rslot_set_fill(chart_tile, 1.0f);
	Rid chart = Rwidget_add_child(chart_tile, custom_panel(paint_chart, 480.0f, 180.0f, panel));
	Rslot_set_fill(chart, 1.0f);

	sync_controls(panel);
	return root;
}

void panel_simulate(Panel* panel, double delta) {
	if (!panel->running || delta <= 0.0) return;
	panel->time += delta;

	const float t = (float)panel->time;
	float pressure_target = panel->pump ? 68.0f : 2.0f;
	if (panel->pump) pressure_target += 2.5f * std::sin(t * 1.7f);

	float flow_target = panel->valve ? (panel->pump ? 44.0f : 12.0f) : 0.0f;
	if (panel->valve) flow_target += 3.0f * std::sin(t * 3.1f);

	panel->pressure += (pressure_target - panel->pressure) * (1.0f - std::exp(-(float)delta / 0.6f));
	panel->flow += (flow_target - panel->flow) * (1.0f - std::exp(-(float)delta / 0.35f));
	if (panel->pressure < 0.0f) panel->pressure = 0.0f;
	if (panel->flow < 0.0f) panel->flow = 0.0f;

	if (panel->time - panel->last_sample >= SAMPLE_INTERVAL) {
		panel->last_sample = panel->time;
		panel->pressure_history[panel->sample_head] = panel->pressure;
		panel->flow_history[panel->sample_head] = panel->flow;
		panel->sample_head = (panel->sample_head + 1) % SAMPLES;
		if (panel->sample_count < SAMPLES) panel->sample_count++;
	}

	panel->alarm.on = panel->armed && panel->pressure > (float)panel->alarm_limit;
}

void panel_refresh(Panel* panel) {
	sync_controls(panel);
}

void panel_reset(Panel* panel) {
	panel->time = 0.0;
	panel->last_sample = 0.0;
	panel->pressure = 2.0f;
	panel->flow = 0.0f;
	panel->sample_count = 0;
	panel->sample_head = 0;
	panel->alarm.on = false;
	std::memset(panel->pressure_history, 0, sizeof(panel->pressure_history));
	std::memset(panel->flow_history, 0, sizeof(panel->flow_history));
	sync_controls(panel);
}

bool panel_running(const Panel* panel) {
	return panel->running;
}

void panel_set_running(Panel* panel, bool running) {
	panel->running = running;
}

bool panel_alarm(const Panel* panel) {
	return panel->armed && panel->pressure > panel->alarm_limit;
}

float panel_pressure(const Panel* panel) {
	return panel->pressure;
}

float panel_flow(const Panel* panel) {
	return panel->flow;
}

int panel_samples(const Panel* panel) {
	return panel->sample_count;
}

void panel_set_alarm_limit(Panel* panel, double bar) {
	if (bar < 10.0) bar = 10.0;
	if (bar > 100.0) bar = 100.0;
	panel->alarm_limit = bar;
	panel->alarm.on = panel->armed && panel->pressure > (float)panel->alarm_limit;
}

double panel_alarm_limit(const Panel* panel) {
	return panel->alarm_limit;
}

void panel_set_window(Panel* panel, double seconds) {
	if (seconds < 1.0) seconds = 1.0;
	if (seconds > 10.0) seconds = 10.0;
	panel->window_seconds = seconds;
	int samples = (int)(seconds * 30.0 + 0.5);
	if (samples < 30) samples = 30;
	if (samples > SAMPLES) samples = SAMPLES;
	panel->visible_samples = samples;
}

double panel_window(const Panel* panel) {
	return panel->window_seconds;
}

void panel_set_switch(Panel* panel, PanelButton which, bool on) {
	switch (which) {
	case PANEL_PUMP: panel->pump = on; break;
	case PANEL_VALVE: panel->valve = on; break;
	case PANEL_INTERLOCKS: panel->armed = on; break;
	}
	sync_controls(panel);
}

bool panel_switch(const Panel* panel, PanelButton which) {
	switch (which) {
	case PANEL_PUMP: return panel->pump;
	case PANEL_VALVE: return panel->valve;
	case PANEL_INTERLOCKS: return panel->armed;
	}
	return false;
}

Rid panel_button(Panel* panel, PanelButton which) {
	switch (which) {
	case PANEL_PUMP: return panel->pump_button;
	case PANEL_VALVE: return panel->valve_button;
	case PANEL_INTERLOCKS: return panel->arm_button;
	}
	return R_INVALID_ID;
}

int panel_paints(const Panel* panel, PanelPaint which) {
	return panel->paints[which];
}
