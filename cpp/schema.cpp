/**
 * The settings schema reader - see schema.h.
 */

#include "schema.h"

#include "json.h"

#include <cstdio>
#include <fstream>
#include <sstream>

int Schema::setting_count() const {
	int count = 0;
	for (const auto& group : groups) count += (int)group.settings.size();
	return count;
}

namespace {

std::string text_of(const Json& object, const char* key, const std::string& fallback = std::string()) {
	const Json* value = object.member(key);
	return value && value->kind == Json::String ? value->text : fallback;
}

std::string number_text(double value) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%g", value);
	return buffer;
}

void read_setting(const Json& property, const std::string& path, SchemaSetting& setting) {
	setting.path = path;
	const std::string leaf = path.substr(path.find('.') + 1);
	setting.title = text_of(property, "title", leaf);
	setting.description = text_of(property, "description");
	setting.type = text_of(property, "type", "string");

	if (const Json* choices = property.member("enum"); choices && choices->kind == Json::Array) {
		for (const auto& choice : choices->items) {
			if (choice.kind == Json::String) setting.choices.push_back(choice.text);
		}
	}
	if (const Json* minimum = property.member("minimum"); minimum && minimum->kind == Json::Number) {
		if (const Json* maximum = property.member("maximum"); maximum && maximum->kind == Json::Number) {
			setting.minimum = minimum->number;
			setting.maximum = maximum->number;
			setting.has_range = true;
		}
	}
	if (const Json* fallback = property.member("default")) {
		if (fallback->kind == Json::Bool) setting.default_text = fallback->boolean ? "true" : "false";
		else if (fallback->kind == Json::Number) setting.default_text = number_text(fallback->number);
		else if (fallback->kind == Json::String) setting.default_text = fallback->text;
	}
}

}  // namespace

Schema schema_load(const std::string& path) {
	Schema schema;
	schema.path = path;

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		schema.error = "not found";
		return schema;
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();

	Json root;
	std::string error;
	if (!json_parse(buffer.str(), root, error)) {
		schema.error = error;
		return schema;
	}
	if (root.kind != Json::Object) {
		schema.error = "the root is not an object";
		return schema;
	}
	schema.title = text_of(root, "title");

	const Json* properties = root.member("properties");
	if (!properties || properties->kind != Json::Object) {
		schema.error = "no properties";
		return schema;
	}

	for (const auto& group_member : properties->members) {
		const Json& group = group_member.second;
		const Json* nested = group.member("properties");
		if (!nested || nested->kind != Json::Object) continue;

		SchemaGroup schema_group;
		schema_group.title = text_of(group, "title", group_member.first);
		for (const auto& setting_member : nested->members) {
			SchemaSetting setting;
			read_setting(setting_member.second, group_member.first + "." + setting_member.first, setting);
			schema_group.settings.push_back(std::move(setting));
		}
		schema.groups.push_back(std::move(schema_group));
	}

	schema.loaded = true;
	return schema;
}
