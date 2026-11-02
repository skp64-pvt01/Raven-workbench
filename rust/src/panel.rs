//! The instrument panel as a reusable module.
//!
//! This is a port of the pane in `cpp/panel/panel.cpp`: gauges, lamps, the
//! strip chart and the simulated plant, with an opaque API so the shell only
//! decides where the pane goes and what the menus do.
//!
//!     panel().build()          the widget tree
//!     panel().simulate(delta)  the plant and the trend
//!     panel().refresh()        the labels, buttons and lamps
//!
//! The panel is one global object: a single-window application has no other
//! owner, and the paint callbacks run on the UI thread.

use raven::*;
use std::ffi::c_void;

const SAMPLES: usize = 300; // ten seconds at 30 Hz
const SAMPLE_INTERVAL: f64 = 1.0 / 30.0;

pub const PANEL_PUMP: i32 = 0;
pub const PANEL_VALVE: i32 = 1;
pub const PANEL_INTERLOCKS: i32 = 2;

pub const PAINT_METER: usize = 0;
pub const PAINT_DIGITAL: usize = 1;
pub const PAINT_CHART: usize = 2;
pub const PAINT_LED: usize = 3;

#[derive(Clone, Copy, Default)]
struct Color {
    r: f32,
    g: f32,
    b: f32,
    a: f32,
}

#[derive(Clone, Copy)]
struct Lamp {
    on: bool,
    r: f32,
    g: f32,
    b: f32,
}

pub struct Panel {
    running: bool,
    pump: bool,
    valve: bool,
    armed: bool,
    time: f64,
    pressure: f32,
    flow: f32,
    pub alarm_limit: f64,
    pub window_seconds: f64,
    visible_samples: usize,

    pressure_history: [f32; SAMPLES],
    flow_history: [f32; SAMPLES],
    sample_count: usize,
    sample_head: usize,
    last_sample: f64,

    // widgets (rebuilt by `build`)
    alarm_label: Rid,
    pump_button: Rid,
    valve_button: Rid,
    arm_button: Rid,
    lamp_widgets: [Rid; 3],

    paints: [u32; 4],
}

impl Default for Panel {
    fn default() -> Self {
        Self {
            running: true,
            pump: false,
            valve: false,
            armed: false,
            time: 0.0,
            pressure: 2.0,
            flow: 0.0,
            alarm_limit: 75.0,
            window_seconds: 10.0,
            visible_samples: SAMPLES,
            pressure_history: [0.0; SAMPLES],
            flow_history: [0.0; SAMPLES],
            sample_count: 0,
            sample_head: 0,
            last_sample: 0.0,
            alarm_label: R_INVALID_ID,
            pump_button: R_INVALID_ID,
            valve_button: R_INVALID_ID,
            arm_button: R_INVALID_ID,
            lamp_widgets: [R_INVALID_ID; 3],
            paints: [0; 4],
        }
    }
}

static mut PANEL: *mut Panel = std::ptr::null_mut();

pub fn panel() -> &'static mut Panel {
    unsafe { &mut *PANEL }
}

pub fn init_panel() {
    unsafe {
        if PANEL.is_null() {
            PANEL = Box::into_raw(Box::new(Panel::default()));
        }
    }
}

pub fn free_panel() {
    unsafe {
        if !PANEL.is_null() {
            drop(Box::from_raw(PANEL));
            PANEL = std::ptr::null_mut();
        }
    }
}

/// The lamps, by the widget that paints them: the C callback passes no Rust
/// object, so the paint function reads the lamp from the table of handles.
static mut LAMPS: [Lamp; 3] = [
    Lamp { on: true, r: 0.30, g: 0.90, b: 0.45 },
    Lamp { on: true, r: 0.30, g: 0.65, b: 1.00 },
    Lamp { on: false, r: 1.00, g: 0.30, b: 0.25 },
];

unsafe fn lamp_for(widget: Rid) -> *mut Lamp {
    let p = panel();
    for index in 0..3 {
        if p.lamp_widgets[index] == widget {
            return &raw mut LAMPS[index];
        }
    }
    std::ptr::null_mut()
}

/* ---------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------- */

fn role(widget: Rid, which: RColorRole) -> Color {
    let (mut r, mut g, mut b, mut a) = (0.0f32, 0.0f32, 0.0f32, 0.0f32);
    unsafe { Rwidget_get_color(widget, which, &mut r, &mut g, &mut b, &mut a) };
    Color { r, g, b, a }
}

fn clamp01(value: f32) -> f32 {
    value.clamp(0.0, 1.0)
}

fn text_width(text: &str, size: f32) -> f32 {
    text.chars().count() as f32 * size * 0.60
}

fn draw_text(painter: *mut RPainter, font: Rid, size: f32, x: f32, y: f32, text: &str, color: Color) {
    unsafe {
        Rdraw_text(painter, font, size, x, y, cstr(text).as_ptr(), color.r, color.g, color.b, color.a)
    };
}

fn draw_text_centered(
    painter: *mut RPainter,
    font: Rid,
    size: f32,
    center_x: f32,
    y: f32,
    text: &str,
    color: Color,
) {
    draw_text(painter, font, size, center_x - text_width(text, size) * 0.5, y, text, color);
}

/* ---------------------------------------------------------------------------
 *  Paint: analog meter
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn paint_meter(widget: Rid, painter: *mut RPainter, width: f32, height: f32, _data: *mut c_void) {
    let p = panel();
    p.paints[PAINT_METER] += 1;

    let surface = role(widget, R_COLOR_PANEL);
    let border = role(widget, R_COLOR_BORDER);
    let text = role(widget, R_COLOR_TEXT);
    let accent = role(widget, R_COLOR_ACCENT);
    let danger = role(widget, R_COLOR_DANGER);
    let track = role(widget, R_COLOR_TRACK);

    Rdraw_rect(painter, 0.0, 0.0, width, height, 14.0, surface.r, surface.g, surface.b, 0.85);
    Rdraw_rect_outline(painter, 0.5, 0.5, width - 1.0, height - 1.0, 14.0, 1.0, border.r, border.g, border.b, 0.7);

    let center_x = width * 0.5;
    let center_y = height * 0.58;
    let radius = width.min(height) * 0.36;
    let start = 2.35619449f32;
    let sweep = 4.71238898f32;

    for i in 0..=10 {
        let angle = start + sweep * i as f32 / 10.0;
        let major = i % 5 == 0;
        let inner = radius * if major { 0.80 } else { 0.88 };
        Rdraw_line(
            painter,
            center_x + angle.cos() * inner,
            center_y + angle.sin() * inner,
            center_x + angle.cos() * radius,
            center_y + angle.sin() * radius,
            if major { 2.0 } else { 1.0 },
            text.r,
            text.g,
            text.b,
            if major { 0.9 } else { 0.4 },
        );
    }

    let value = clamp01(p.pressure / 100.0);
    for i in 0..60 {
        let a0 = start + sweep * i as f32 / 60.0;
        let a1 = start + sweep * (i + 1) as f32 / 60.0;
        let lit = (i + 1) as f32 / 60.0 <= value;
        let color = if lit { accent } else { track };
        Rdraw_line(
            painter,
            center_x + a0.cos() * radius * 0.92,
            center_y + a0.sin() * radius * 0.92,
            center_x + a1.cos() * radius * 0.92,
            center_y + a1.sin() * radius * 0.92,
            4.0,
            color.r,
            color.g,
            color.b,
            color.a * if lit { 0.9 } else { 0.5 },
        );
    }

    let angle = start + sweep * value;
    let needle = if p.pressure > p.alarm_limit as f32 { danger } else { text };
    Rdraw_line(
        painter,
        center_x,
        center_y,
        center_x + angle.cos() * radius * 0.78,
        center_y + angle.sin() * radius * 0.78,
        3.0,
        needle.r,
        needle.g,
        needle.b,
        1.0,
    );
    Rdraw_circle(painter, center_x, center_y, 6.0, needle.r, needle.g, needle.b, 1.0);
    Rdraw_circle(painter, center_x, center_y, 2.5, surface.r, surface.g, surface.b, 1.0);

    draw_text(painter, R_INVALID_ID, 12.0, 14.0, 10.0, "PRESSURE  P-101", text);
    draw_text_centered(
        painter,
        Rfont_get_monospace(),
        26.0,
        center_x,
        height - 40.0,
        &format!("{:.1} bar", p.pressure),
        accent,
    );
}

/* ---------------------------------------------------------------------------
 *  Paint: digital meter
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn paint_digital(widget: Rid, painter: *mut RPainter, width: f32, height: f32, _data: *mut c_void) {
    let p = panel();
    p.paints[PAINT_DIGITAL] += 1;

    let surface = role(widget, R_COLOR_PANEL);
    let border = role(widget, R_COLOR_BORDER);
    let text = role(widget, R_COLOR_TEXT);
    let accent = role(widget, R_COLOR_ACCENT);

    Rdraw_rect(painter, 0.0, 0.0, width, height, 14.0, surface.r, surface.g, surface.b, 0.85);
    Rdraw_rect_outline(painter, 0.5, 0.5, width - 1.0, height - 1.0, 14.0, 1.0, border.r, border.g, border.b, 0.7);

    Rdraw_rect(painter, 14.0, 34.0, width - 28.0, height - 62.0, 8.0, 0.05, 0.07, 0.09, 0.85);
    Rdraw_rect_outline(painter, 14.5, 34.5, width - 29.0, height - 63.0, 8.0, 1.0, border.r, border.g, border.b, 0.5);

    let size = height * 0.26;
    draw_text_centered(
        painter,
        Rfont_get_monospace(),
        size,
        width * 0.5,
        34.0 + (height - 62.0 - size) * 0.45,
        &format!("{:5.1}", p.flow),
        accent,
    );

    draw_text(painter, R_INVALID_ID, 12.0, 14.0, 10.0, "FLOW  F-201", text);
    draw_text(painter, R_INVALID_ID, 12.0, width - 58.0, 10.0, "L/min", text);

    Rdraw_rect(painter, 20.0, height - 18.0, width - 40.0, 3.0, 1.5, border.r, border.g, border.b, 0.35);
    Rdraw_rect(
        painter,
        20.0,
        height - 18.0,
        (width - 40.0) * clamp01(p.flow / 100.0),
        3.0,
        1.5,
        accent.r,
        accent.g,
        accent.b,
        0.9,
    );
}

/* ---------------------------------------------------------------------------
 *  Paint: lamp
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn paint_led(widget: Rid, painter: *mut RPainter, width: f32, height: f32, _data: *mut c_void) {
    let led = lamp_for(widget);
    if led.is_null() {
        return;
    }
    let led = &*led;
    panel().paints[PAINT_LED] += 1;

    let center_x = width * 0.5;
    let center_y = height * 0.5;
    let radius = width.min(height) * 0.30;
    let strength = if led.on { 1.0 } else { 0.0 };

    for i in 0..3 {
        let r = radius * (1.4 + 0.35 * i as f32);
        Rdraw_circle(painter, center_x, center_y, r, led.r, led.g, led.b, (0.10 - 0.03 * i as f32) * strength);
    }
    Rdraw_circle(
        painter,
        center_x,
        center_y,
        radius,
        led.r * (0.35 + 0.65 * strength),
        led.g * (0.35 + 0.65 * strength),
        led.b * (0.35 + 0.65 * strength),
        0.95,
    );
    Rdraw_circle(
        painter,
        center_x - radius * 0.25,
        center_y - radius * 0.30,
        radius * 0.28,
        1.0,
        1.0,
        1.0,
        0.18 + 0.22 * strength,
    );
}

/* ---------------------------------------------------------------------------
 *  Paint: real-time strip chart
 * ------------------------------------------------------------------------- */

unsafe extern "C" fn paint_chart(widget: Rid, painter: *mut RPainter, width: f32, height: f32, _data: *mut c_void) {
    let p = panel();
    p.paints[PAINT_CHART] += 1;

    let surface = role(widget, R_COLOR_PANEL);
    let border = role(widget, R_COLOR_BORDER);
    let text = role(widget, R_COLOR_TEXT);
    let accent = role(widget, R_COLOR_ACCENT);
    let success = role(widget, R_COLOR_SUCCESS);

    Rdraw_rect(painter, 0.0, 0.0, width, height, 14.0, surface.r, surface.g, surface.b, 0.85);
    Rdraw_rect_outline(painter, 0.5, 0.5, width - 1.0, height - 1.0, 14.0, 1.0, border.r, border.g, border.b, 0.7);

    let (left, right, top, bottom) = (52.0f32, 18.0f32, 34.0f32, 22.0f32);
    let plot_width = width - left - right;
    let plot_height = height - top - bottom;

    draw_text(painter, R_INVALID_ID, 12.0, 14.0, 10.0, &format!("TREND  {:.0} s", p.window_seconds), text);
    Rdraw_rect(painter, width - 128.0, 14.0, 9.0, 9.0, 2.0, accent.r, accent.g, accent.b, 0.9);
    draw_text(painter, R_INVALID_ID, 11.0, width - 114.0, 11.0, "pressure", text);
    Rdraw_rect(painter, width - 52.0, 14.0, 9.0, 9.0, 2.0, success.r, success.g, success.b, 0.9);
    draw_text(painter, R_INVALID_ID, 11.0, width - 38.0, 11.0, "flow", text);

    for i in 0..=4 {
        let y = top + plot_height * i as f32 / 4.0;
        Rdraw_line(painter, left, y, left + plot_width, y, 1.0, border.r, border.g, border.b, if i == 4 { 0.6 } else { 0.22 });
        draw_text(painter, Rfont_get_monospace(), 10.0, 16.0, y - 6.0, &format!("{}", 100 - 25 * i), text);
    }
    for i in 0..=10 {
        let x = left + plot_width * i as f32 / 10.0;
        Rdraw_line(painter, x, top, x, top + plot_height, 1.0, border.r, border.g, border.b, 0.12);
    }

    if p.sample_count < 2 {
        return;
    }

    let visible = p.visible_samples.min(p.sample_count);
    let step_x = plot_width / (p.visible_samples - 1) as f32;
    let mut previous_pressure = (0.0f32, 0.0f32);
    let mut previous_flow = (0.0f32, 0.0f32);

    for i in 0..visible {
        let index = (p.sample_head + SAMPLES - visible + i) % SAMPLES;
        let x = left + plot_width - step_x * (visible - 1 - i) as f32;
        let pressure_y = top + plot_height * (1.0 - clamp01(p.pressure_history[index] / 100.0));
        let flow_y = top + plot_height * (1.0 - clamp01(p.flow_history[index] / 100.0));

        if i > 0 {
            Rdraw_line(painter, previous_pressure.0, previous_pressure.1, x, pressure_y, 1.8, accent.r, accent.g, accent.b, 0.95);
            Rdraw_line(painter, previous_flow.0, previous_flow.1, x, flow_y, 1.8, success.r, success.g, success.b, 0.95);
        }
        previous_pressure = (x, pressure_y);
        previous_flow = (x, flow_y);
    }

    if p.armed {
        let y = top + plot_height * (1.0 - (p.alarm_limit / 100.0) as f32);
        Rdraw_line(painter, left, y, left + plot_width, y, 1.5, 0.95, 0.35, 0.30, 0.5);
        draw_text(painter, R_INVALID_ID, 10.0, left + 6.0, y - 14.0, &format!("alarm {:.0}", p.alarm_limit), text);
    }
}

/* ---------------------------------------------------------------------------
 *  Controls
 * ------------------------------------------------------------------------- */

pub fn sync_controls() {
    let p = panel();
    unsafe {
        Rbutton_set_text(p.pump_button, cstr(if p.pump { "PUMP  ON" } else { "PUMP  OFF" }).as_ptr());
        Rbutton_set_style(p.pump_button, if p.pump { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT });
        Rbutton_set_text(p.valve_button, cstr(if p.valve { "VALVE  OPEN" } else { "VALVE  SHUT" }).as_ptr());
        Rbutton_set_style(p.valve_button, if p.valve { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT });
        Rbutton_set_text(p.arm_button, cstr(if p.armed { "INTERLOCKS  ARMED" } else { "INTERLOCKS  OFF" }).as_ptr());
        Rbutton_set_style(p.arm_button, if p.armed { R_BUTTON_PRIMARY } else { R_BUTTON_DEFAULT });

        let (message, color) = if p.armed && p.pressure > p.alarm_limit as f32 {
            ("HIGH PRESSURE", Color { r: 1.0, g: 0.35, b: 0.30, a: 1.0 })
        } else if !p.armed {
            ("interlocks off", Color { r: 0.75, g: 0.75, b: 0.75, a: 0.8 })
        } else {
            ("interlocks armed", Color { r: 0.45, g: 0.85, b: 0.50, a: 0.9 })
        };
        Rlabel_set_text(p.alarm_label, cstr(message).as_ptr());
        Rlabel_set_color(p.alarm_label, color.r, color.g, color.b, color.a);
        LAMPS[2].on = p.armed && p.pressure > p.alarm_limit as f32;
    }
}

unsafe extern "C" fn on_pump(_button: Rid, _data: *mut c_void) {
    panel().pump = !panel().pump;
    sync_controls();
}

unsafe extern "C" fn on_valve(_button: Rid, _data: *mut c_void) {
    panel().valve = !panel().valve;
    sync_controls();
}

unsafe extern "C" fn on_arm(_button: Rid, _data: *mut c_void) {
    panel().armed = !panel().armed;
    sync_controls();
}

fn tile() -> Rid {
    let column = unsafe { Rwidget_create(R_WIDGET_VBOX) };
    unsafe {
        Rpanel_set_spacing(column, 8.0);
        Rslot_set_padding(column, 12.0, 12.0, 12.0, 12.0);
    }
    column
}

fn custom_panel(paint: RPaintCallback, min_width: f32, min_height: f32) -> Rid {
    let widget = unsafe { Rwidget_create(R_WIDGET_CUSTOM) };
    unsafe {
        Rcustom_set_paint_callback(widget, paint, std::ptr::null_mut());
        Rwidget_set_min_size(widget, min_width, min_height);
        Rwidget_set_hit_testable(widget, 0);
    }
    widget
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

impl Panel {
    /// Builds the instruments; returns the root widget of the pane.
    pub fn build(&mut self) -> Rid {
        unsafe {
            let root = Rwidget_create(R_WIDGET_VBOX);
            Rpanel_set_spacing(root, 12.0);

            let row = Rwidget_add_child(root, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(row, 12.0);

            let gauge_tile = Rwidget_add_child(row, tile());
            Rslot_set_fill(gauge_tile, 1.0);
            let gauge = Rwidget_add_child(gauge_tile, custom_panel(Some(paint_meter), 240.0, 240.0));
            Rslot_set_fill(gauge, 1.0);

            let flow_tile = Rwidget_add_child(row, tile());
            Rslot_set_fill(flow_tile, 1.0);
            let flow = Rwidget_add_child(flow_tile, custom_panel(Some(paint_digital), 240.0, 150.0));
            Rslot_set_fill(flow, 1.0);

            let control_tile = Rwidget_add_child(row, tile());
            Rslot_set_fill(control_tile, 1.0);

            let leds = Rwidget_add_child(control_tile, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(leds, 10.0);
            Rslot_set_halign(leds, R_HALIGN_CENTER);

            for (index, name) in ["POWER", "LINK", "ALARM"].iter().enumerate() {
                let column = Rwidget_add_child(leds, Rwidget_create(R_WIDGET_VBOX));
                Rpanel_set_spacing(column, 2.0);
                let lamp = Rwidget_add_child(column, custom_panel(Some(paint_led), 46.0, 46.0));
                Rwidget_set_size(lamp, 46.0, 46.0);
                self.lamp_widgets[index] = lamp;
                let label = Rwidget_add_child(column, Rlabel_create(cstr(name).as_ptr()));
                Rlabel_set_text_size(label, 10.0);
                Rslot_set_halign(label, R_HALIGN_CENTER);
            }

            self.alarm_label = Rwidget_add_child(control_tile, Rlabel_create(cstr("interlocks off").as_ptr()));
            Rlabel_set_text_size(self.alarm_label, 12.0);
            Rslot_set_halign(self.alarm_label, R_HALIGN_CENTER);

            self.pump_button = Rwidget_add_child(control_tile, Rbutton_create(cstr("PUMP  OFF").as_ptr()));
            Rbutton_set_clicked_callback(self.pump_button, Some(on_pump), std::ptr::null_mut());
            self.valve_button = Rwidget_add_child(control_tile, Rbutton_create(cstr("VALVE  SHUT").as_ptr()));
            Rbutton_set_clicked_callback(self.valve_button, Some(on_valve), std::ptr::null_mut());
            self.arm_button = Rwidget_add_child(control_tile, Rbutton_create(cstr("INTERLOCKS  OFF").as_ptr()));
            Rbutton_set_clicked_callback(self.arm_button, Some(on_arm), std::ptr::null_mut());

            let spacer = Rwidget_add_child(control_tile, Rwidget_create(R_WIDGET_SPACER));
            Rslot_set_fill(spacer, 1.0);

            let chart_tile = Rwidget_add_child(root, tile());
            Rslot_set_fill(chart_tile, 1.0);
            let chart = Rwidget_add_child(chart_tile, custom_panel(Some(paint_chart), 480.0, 180.0));
            Rslot_set_fill(chart, 1.0);

            sync_controls();
            root
        }
    }

    /// Advances the simulated plant and the trend buffer.
    pub fn simulate(&mut self, delta: f64) {
        if !self.running || delta <= 0.0 {
            return;
        }
        self.time += delta;
        let t = self.time as f32;

        let mut pressure_target = if self.pump { 68.0 } else { 2.0 };
        if self.pump {
            pressure_target += 2.5 * (t * 1.7).sin();
        }
        let mut flow_target = if self.valve { if self.pump { 44.0 } else { 12.0 } } else { 0.0 };
        if self.valve {
            flow_target += 3.0 * (t * 3.1).sin();
        }

        self.pressure += (pressure_target - self.pressure) * (1.0 - (-(delta as f32) / 0.6).exp());
        self.flow += (flow_target - self.flow) * (1.0 - (-(delta as f32) / 0.35).exp());
        self.pressure = self.pressure.max(0.0);
        self.flow = self.flow.max(0.0);

        if self.time - self.last_sample >= SAMPLE_INTERVAL {
            self.last_sample = self.time;
            self.pressure_history[self.sample_head] = self.pressure;
            self.flow_history[self.sample_head] = self.flow;
            self.sample_head = (self.sample_head + 1) % SAMPLES;
            if self.sample_count < SAMPLES {
                self.sample_count += 1;
            }
        }
    }

    /// Copies the state into the labels, buttons and lamps.
    pub fn refresh(&mut self) {
        sync_controls();
    }

    /// Back to a cold plant and an empty trend; the switches keep their position.
    pub fn reset(&mut self) {
        self.time = 0.0;
        self.last_sample = 0.0;
        self.pressure = 2.0;
        self.flow = 0.0;
        self.sample_count = 0;
        self.sample_head = 0;
        self.pressure_history = [0.0; SAMPLES];
        self.flow_history = [0.0; SAMPLES];
        sync_controls();
    }

    pub fn running(&self) -> bool {
        self.running
    }

    pub fn set_running(&mut self, running: bool) {
        self.running = running;
    }

    pub fn alarm(&self) -> bool {
        self.armed && self.pressure > self.alarm_limit as f32
    }

    pub fn pressure(&self) -> f32 {
        self.pressure
    }

    pub fn flow(&self) -> f32 {
        self.flow
    }

    pub fn samples(&self) -> usize {
        self.sample_count
    }

    /// The interlock threshold (bar), clamped to the range the schema offers.
    pub fn set_alarm_limit(&mut self, bar: f64) {
        self.alarm_limit = bar.clamp(10.0, 100.0);
    }

    /// How many seconds the trend shows, clamped to 1..10.
    pub fn set_window(&mut self, seconds: f64) {
        self.window_seconds = seconds.clamp(1.0, 10.0);
        let samples = (self.window_seconds * 30.0 + 0.5) as usize;
        self.visible_samples = samples.clamp(30, SAMPLES);
    }

    pub fn set_switch(&mut self, which: i32, on: bool) {
        match which {
            PANEL_PUMP => self.pump = on,
            PANEL_VALVE => self.valve = on,
            PANEL_INTERLOCKS => self.armed = on,
            _ => {}
        }
        sync_controls();
    }

    pub fn switch(&self, which: i32) -> bool {
        match which {
            PANEL_PUMP => self.pump,
            PANEL_VALVE => self.valve,
            PANEL_INTERLOCKS => self.armed,
            _ => false,
        }
    }

    pub fn button(&self, which: i32) -> Rid {
        match which {
            PANEL_PUMP => self.pump_button,
            PANEL_VALVE => self.valve_button,
            PANEL_INTERLOCKS => self.arm_button,
            _ => R_INVALID_ID,
        }
    }

    pub fn paints(&self, which: usize) -> u32 {
        self.paints[which]
    }
}
