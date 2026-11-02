/**
 * The Preferences pane: one row per setting of the schema.
 *
 * The pane is built from workbench.schema.json (see schema.h). Each row shows
 * the title and description of the setting and a control of the right type:
 * a check box for a boolean, a combo box for an enum, a spin box for a number
 * with a range, a text field otherwise. Changes are written into the Config and
 * reported through `changed`, so the application can apply them at once; the
 * file is only written by `preferences_save`.
 */

#ifndef WORKBENCH_PREFERENCES_H
#define WORKBENCH_PREFERENCES_H

#include <Raven.h>

#include <string>

#include "config.h"
#include "schema.h"

struct PreferencesCallbacks {
	/** A setting changed: apply what can be applied without a restart. */
	void (*changed)(void* user);
	/** A message for the status bar. */
	void (*status)(void* user, const char* text);
	void* user;
};

struct Preferences;

Preferences* preferences_create(Config* config, const Schema* schema, PreferencesCallbacks callbacks);
Rid preferences_build(Preferences* preferences);
void preferences_destroy(Preferences* preferences);

/** Writes the configuration file; false (and a message) when it cannot. */
bool preferences_save(Preferences* preferences);

/** Back to the defaults of the application (not written to the file yet). */
void preferences_reset(Preferences* preferences);

/** Filters the rows: a substring of the title, the description or the key. */
void preferences_set_filter(Preferences* preferences, const char* text);

/** Rows matching the current filter (tests). */
int preferences_shown(const Preferences* preferences);

#endif
