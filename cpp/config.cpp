/**
 * The workbench settings - see config.h.
 */

#include "config.h"

#include "json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

bool read_file(const std::string& path, std::string& text) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	std::ostringstream buffer;
	buffer << file.rdbuf();
	text = buffer.str();
	return true;
}

const char* kind_name(Json::Kind kind) {
	switch (kind) {
	case Json::Null: return "null";
	case Json::Bool: return "a boolean";
	case Json::Number: return "a number";
	case Json::String: return "a string";
	case Json::Array: return "an array";
	case Json::Object: return "an object";
	}
	return "a value";
}

std::string escape(const std::string& text) {
	std::string out;
	for (char c : text) {
		if (c == '\\' || c == '"') out += '\\';
		out += c;
	}
	return out;
}

std::string number_text(double value) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%g", value);
	return buffer;
}

int apply(const Json& root, Config& config) {
	int applied = 0;

	if (const Json* release = root.member("release")) {
		if (const Json* channel = release->member("channel"); channel && channel->kind == Json::String) {
			config.channel = channel->text;
			applied++;
		}
	}

	if (const Json* ui = root.member("ui")) {
		if (const Json* theme = ui->member("theme"); theme && theme->kind == Json::String) {
			if (theme->text == "dark" || theme->text == "light") {
				config.theme = theme->text;
				applied++;
			}
		}
		if (const Json* toolbar = ui->member("toolbar"); toolbar && toolbar->kind == Json::Bool) {
			config.toolbar = toolbar->boolean;
			applied++;
		}
	}

	if (const Json* startup = root.member("startup")) {
		if (const Json* pane = startup->member("pane"); pane && pane->kind == Json::String) {
			if (pane->text == "instrument-panel") config.pane = 1;
			else if (pane->text == "application") config.pane = 0;
			applied++;
		}
		if (const Json* pump = startup->member("pump"); pump && pump->kind == Json::Bool) {
			config.pump = pump->boolean;
			applied++;
		}
		if (const Json* valve = startup->member("valve"); valve && valve->kind == Json::Bool) {
			config.valve = valve->boolean;
			applied++;
		}
		if (const Json* armed = startup->member("armed"); armed && armed->kind == Json::Bool) {
			config.armed = armed->boolean;
			applied++;
		}
	}

	if (const Json* simulation = root.member("simulation")) {
		if (const Json* limit = simulation->member("alarm_limit"); limit && limit->kind == Json::Number) {
			double value = limit->number;
			if (value < 10.0) value = 10.0;
			if (value > 100.0) value = 100.0;
			config.alarm_limit = value;
			applied++;
		}
		if (const Json* seconds = simulation->member("chart_seconds"); seconds && seconds->kind == Json::Number) {
			double value = seconds->number;
			if (value < 1.0) value = 1.0;
			if (value > 10.0) value = 10.0;
			config.chart_seconds = value;
			applied++;
		}
	}

	return applied;
}

}  // namespace

std::string Config::status() const {
	if (!loaded && error.empty()) return "not found, defaults";
	if (!error.empty()) return error + ", defaults";
	return std::to_string(settings) + " setting" + (settings == 1 ? "" : "s") + " loaded";
}

Config config_defaults(const std::string& path) {
	Config config;
	config.path = path;
	return config;
}

Config config_load(const std::string& path) {
	Config config = config_defaults(path);

	std::string text;
	if (!read_file(path, text)) return config;  /* not found: defaults, no error */

	Json root;
	std::string error;
	if (!json_parse(text, root, error)) {
		config.error = error;
		return config;
	}
	if (root.kind != Json::Object) {
		config.error = std::string("the root is ") + kind_name(root.kind) + ", expected an object";
		return config;
	}

	config.settings = apply(root, config);
	config.loaded = true;
	return config;
}

bool config_get(const Config& config, const std::string& path, std::string& value) {
	if (path == "release.channel") value = config.channel;
	else if (path == "ui.theme") value = config.theme;
	else if (path == "ui.toolbar") value = config.toolbar ? "true" : "false";
	else if (path == "startup.pane") value = config.pane == 1 ? "instrument-panel" : "application";
	else if (path == "startup.pump") value = config.pump ? "true" : "false";
	else if (path == "startup.valve") value = config.valve ? "true" : "false";
	else if (path == "startup.armed") value = config.armed ? "true" : "false";
	else if (path == "simulation.alarm_limit") value = number_text(config.alarm_limit);
	else if (path == "simulation.chart_seconds") value = number_text(config.chart_seconds);
	else return false;
	return true;
}

bool config_set(Config& config, const std::string& path, const std::string& value) {
	if (path == "release.channel") {
		config.channel = value;
	} else if (path == "ui.theme") {
		if (value != "dark" && value != "light") return false;
		config.theme = value;
	} else if (path == "ui.toolbar") {
		config.toolbar = value == "true";
	} else if (path == "startup.pane") {
		if (value != "application" && value != "instrument-panel") return false;
		config.pane = value == "instrument-panel" ? 1 : 0;
	} else if (path == "startup.pump") {
		config.pump = value == "true";
	} else if (path == "startup.valve") {
		config.valve = value == "true";
	} else if (path == "startup.armed") {
		config.armed = value == "true";
	} else if (path == "simulation.alarm_limit") {
		double number = std::strtod(value.c_str(), nullptr);
		if (number < 10.0) number = 10.0;
		if (number > 100.0) number = 100.0;
		config.alarm_limit = number;
	} else if (path == "simulation.chart_seconds") {
		double number = std::strtod(value.c_str(), nullptr);
		if (number < 1.0) number = 1.0;
		if (number > 10.0) number = 10.0;
		config.chart_seconds = number;
	} else {
		return false;
	}
	return true;
}

namespace {

struct Edit {
	std::size_t start;
	std::size_t end;
	std::string text;
};

const char* const kStringPaths[] = {
	"release.channel", "ui.theme", "startup.pane",
};

bool is_string_path(const std::string& path) {
	for (const char* candidate : kStringPaths) {
		if (path == candidate) return true;
	}
	return false;
}

/** The literal to write for a setting: a quoted JSON string, or true/false/75. */
std::string value_text(const Config& config, const std::string& path, bool& known) {
	std::string value;
	known = config_get(config, path, value);
	if (!known) return std::string();
	if (is_string_path(path)) return "\"" + escape(value) + "\"";
	return value;
}

bool write_file(const std::string& path, const std::string& text, std::string& error) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file << text;
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	return true;
}

/**
 * Rewrites the values of the settings in the text the user wrote: comments,
 * blank lines and the layout of everything else stay exactly as they are.
 * A key the file does not have is added to its object.
 */
bool save_in_place(const std::string& path, std::string& text, const Json& root,
	const Config& config, std::string& error) {
	static const char* const paths[] = {
		"release.channel", "ui.theme", "ui.toolbar",
		"startup.pane", "startup.pump", "startup.valve", "startup.armed",
		"simulation.alarm_limit", "simulation.chart_seconds"
	};

	struct Pending {
		std::string group;
		std::vector<std::string> members;
	};
	std::vector<Edit> edits;
	std::vector<Pending> pending;

	for (const char* path_text : paths) {
		const std::string path = path_text;
		bool known = false;
		const std::string replacement = value_text(config, path, known);
		if (!known) continue;

		const std::size_t dot = path.find('.');
		const std::string group_name = path.substr(0, dot);
		const std::string leaf = path.substr(dot + 1);

		const Json* group = root.member(group_name);
		if (!group || group->kind != Json::Object) {
			error = "the file has no \"" + group_name + "\" object; fix it before saving";
			return false;
		}
		if (const Json* member = group->member(leaf)) {
			const std::string current = text.substr(member->start, member->end - member->start);
			if (current != replacement) edits.push_back({member->start, member->end, replacement});
			continue;
		}

		auto found = std::find_if(pending.begin(), pending.end(),
			[&](const Pending& entry) { return entry.group == group_name; });
		if (found == pending.end()) pending.push_back({group_name, {}});
		found = std::find_if(pending.begin(), pending.end(),
			[&](const Pending& entry) { return entry.group == group_name; });
		found->members.push_back("\"" + leaf + "\": " + replacement);
	}

	for (const Pending& entry : pending) {
		const Json* group = root.member(entry.group);
		const std::size_t close = group->end - 1;  /* the closing brace */
		bool comma = false;
		if (!group->members.empty()) {
			const Json& last = group->members.back().second;
			for (std::size_t i = last.end; i < close && i < text.size(); i++) {
				if (text[i] == ',') {
					comma = true;
					break;
				}
			}
		}
		std::string snippet;
		for (std::size_t i = 0; i < entry.members.size(); i++) {
			if (i == 0) snippet += (group->members.empty() || comma) ? "\n\t\t" : ",\n\t\t";
			else snippet += ",\n\t\t";
			snippet += entry.members[i];
		}
		if (group->members.empty()) snippet += "\n\t";
		edits.push_back({close, close, snippet});
	}

	/* Largest offset first: replacing a value never moves an earlier one. */
	std::sort(edits.begin(), edits.end(),
		[](const Edit& a, const Edit& b) { return a.start > b.start; });
	for (const Edit& edit : edits) text.replace(edit.start, edit.end - edit.start, edit.text);

	/* A safety net: never write a file the reader would reject. */
	Json check;
	std::string check_error;
	if (!json_parse(text, check, check_error)) {
		error = "the change would make the file invalid (" + check_error + "); nothing was written";
		return false;
	}
	return write_file(path, text, error);
}

/** No file yet (or it could not be read): write a fresh one, with comments. */
bool write_new(const std::string& path, const Config& config, std::string& error) {
	std::string text;
	text += "// Raven Workbench - settings written by the Preferences pane.\n";
	text += "// JSON with comments. workbench.schema.json describes every key.\n";
	text += "{\n";
	text += "\t\"release\": { \"channel\": \"" + escape(config.channel) + "\" },\n\n";
	text += "\t\"ui\": {\n";
	text += "\t\t\"theme\": \"" + escape(config.theme) + "\",\n";
	text += "\t\t\"toolbar\": " + std::string(config.toolbar ? "true" : "false") + "\n";
	text += "\t},\n\n";
	text += "\t\"startup\": {\n";
	text += "\t\t\"pane\": \"" + std::string(config.pane ? "instrument-panel" : "application") + "\",\n";
	text += "\t\t\"pump\": " + std::string(config.pump ? "true" : "false") + ",\n";
	text += "\t\t\"valve\": " + std::string(config.valve ? "true" : "false") + ",\n";
	text += "\t\t\"armed\": " + std::string(config.armed ? "true" : "false") + "\n";
	text += "\t},\n\n";
	text += "\t\"simulation\": {\n";
	text += "\t\t\"alarm_limit\": " + number_text(config.alarm_limit) + ",\n";
	text += "\t\t\"chart_seconds\": " + number_text(config.chart_seconds) + "\n";
	text += "\t}\n";
	text += "}\n";
	return write_file(path, text, error);
}

}  // namespace

bool config_save(const std::string& path, const Config& config, std::string& error) {
	std::string text;
	if (read_file(path, text)) {
		Json root;
		std::string parse_error;
		if (!json_parse(text, root, parse_error)) {
			error = "the file has a syntax error (" + parse_error + "); it was not overwritten";
			return false;
		}
		if (root.kind != Json::Object) {
			error = "the file is not a JSON object; it was not overwritten";
			return false;
		}
		return save_in_place(path, text, root, config, error);
	}
	return write_new(path, config, error);
}
