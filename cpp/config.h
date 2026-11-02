/**
 * Application configuration, read from a JSONC file (JSON with comments).
 *
 * The workbench keeps its own small reader: no dependency, and the file is
 * meant to be edited by hand, so comments and trailing commas are accepted.
 *
 *     // workbench.jsonc
 *     {
 *         "ui": { "theme": "light", "toolbar": true },
 *         "startup": { "pane": "application", "pump": true },
 *         "simulation": { "alarm_limit": 75, "chart_seconds": 10 }
 *     }
 *
 * Every field is optional: a missing file, a missing key or a wrong type keeps
 * the default, and the loader reports what it did in `status`.
 */

#ifndef WORKBENCH_CONFIG_H
#define WORKBENCH_CONFIG_H

#include <string>

struct Config {
	/* what happened */
	std::string path;                 /* the file looked for */
	bool loaded = false;              /* read and parsed */
	std::string error;                /* why not, when it failed */
	int settings = 0;                 /* recognised keys that were applied */

	/* the settings */
	std::string channel = "dev";      /* release channel shown in the application pane */
	std::string theme = "dark";       /* "dark" or "light" */
	bool toolbar = true;              /* show the toolbar */
	int pane = 0;                     /* 0 = application, 1 = instrument panel */
	bool pump = false;                /* the plant's switches at startup */
	bool valve = false;
	bool armed = false;
	double alarm_limit = 75.0;        /* bar, the interlock threshold */
	double chart_seconds = 10.0;      /* the trend window */

	/* One line for the status bar and the application pane. */
	std::string status() const;
};

/** The defaults, with `path` recorded and no error. */
Config config_defaults(const std::string& path);

/** Reads a JSONC file. On failure returns the defaults with `error` set. */
Config config_load(const std::string& path);

/** Reads one setting by path ("ui.theme"). False when this build does not use it. */
bool config_get(const Config& config, const std::string& path, std::string& value);

/** Writes one setting from its text form ("true", "dark", "75"). Clamps numbers. */
bool config_set(Config& config, const std::string& path, const std::string& value);

/** Writes the whole configuration, with comments. False (and `error`) on failure. */
bool config_save(const std::string& path, const Config& config, std::string& error);

#endif
