/**
 * The settings schema, read from workbench.schema.json.
 *
 * The Preferences pane is built from it: each group and each setting provides
 * its title, description, type, range or choices, and default. The reader
 * understands the subset this application needs - object properties, boolean /
 * string / number / integer, enum, minimum and maximum - and ignores the rest
 * of JSON Schema, so the same file stays a valid schema for editors.
 */

#ifndef WORKBENCH_SCHEMA_H
#define WORKBENCH_SCHEMA_H

#include <string>
#include <vector>

struct SchemaSetting {
	std::string path;          /* "simulation.alarm_limit" */
	std::string title;
	std::string description;
	std::string type;          /* "boolean", "string", "number", "integer" */
	std::vector<std::string> choices;
	double minimum = 0.0;
	double maximum = 0.0;
	bool has_range = false;
	std::string default_text;
};

struct SchemaGroup {
	std::string title;
	std::vector<SchemaSetting> settings;
};

struct Schema {
	std::string path;
	std::string title;
	std::string error;
	bool loaded = false;
	std::vector<SchemaGroup> groups;

	int setting_count() const;
};

Schema schema_load(const std::string& path);

#endif
