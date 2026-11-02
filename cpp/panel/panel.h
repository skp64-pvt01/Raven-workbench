/**
 * The instrument panel as a reusable module.
 *
 * This is a copy of Examples/InstrumentPanel/cpp/panel.h, kept inside the
 * Workbench so that this folder builds, and can be forked, on its own. When
 * the panel changes, change both copies (or treat this one as the canonical
 * one once the Workbench is its own project).
 *
 * `panel_build` returns a widget tree (gauges, LEDs, controls, chart) that any
 * window can put in a pane; the shell owns the window, the menus, the toolbar
 * and the status bar, the panel owns the plant and its instruments.
 *
 *     Panel* panel = panel_create();
 *     Rid pane = panel_build(panel);
 *     Rwidget_add_child(central, pane);
 *     ...
 *     // every frame, or whenever it is visible
 *     panel_simulate(panel, delta);
 *     panel_refresh(panel);
 *
 * The accessors are for the shell (a status bar) and for tests: the panel's
 * internals stay private.
 */

#ifndef INSTRUMENT_PANEL_H
#define INSTRUMENT_PANEL_H

#include <Raven.h>

struct Panel;

enum PanelButton {
	PANEL_PUMP = 0,
	PANEL_VALVE = 1,
	PANEL_INTERLOCKS = 2
};

enum PanelPaint {
	PANEL_PAINT_METER = 0,
	PANEL_PAINT_DIGITAL = 1,
	PANEL_PAINT_CHART = 2,
	PANEL_PAINT_LED = 3
};

Panel* panel_create();
void panel_destroy(Panel* panel);

/** Builds the instruments; returns the root widget of the pane. */
Rid panel_build(Panel* panel);

/** Advances the simulated plant and the trend buffer. */
void panel_simulate(Panel* panel, double delta);

/** Copies the state into the labels, buttons and lamps. */
void panel_refresh(Panel* panel);

/** Back to a cold plant and an empty trend; the switches keep their position. */
void panel_reset(Panel* panel);

bool panel_running(const Panel* panel);
void panel_set_running(Panel* panel, bool running);
bool panel_alarm(const Panel* panel);

float panel_pressure(const Panel* panel);
float panel_flow(const Panel* panel);
int panel_samples(const Panel* panel);

/* The application can configure the panel: the interlock threshold (bar) and
 * how many seconds the trend shows (1..10). */
void panel_set_alarm_limit(Panel* panel, double bar);
double panel_alarm_limit(const Panel* panel);
void panel_set_window(Panel* panel, double seconds);
double panel_window(const Panel* panel);

/** The panel's switches, for a configuration file or a menu (and tests). */
void panel_set_switch(Panel* panel, PanelButton which, bool on);
bool panel_switch(const Panel* panel, PanelButton which);

/** Widget of one of the panel's own push buttons (for menus and tests). */
Rid panel_button(Panel* panel, PanelButton which);

/** How many times a paint callback ran (tests). */
int panel_paints(const Panel* panel, PanelPaint which);

#endif
