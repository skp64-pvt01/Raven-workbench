/**
 * A small JSONC value and reader, shared by the settings and the schema.
 *
 * Supports objects, arrays, strings with the usual escapes, numbers,
 * true/false/null, `//` and block comments, and trailing commas - what a
 * hand-edited configuration file and a JSON Schema both need.
 */

#ifndef WORKBENCH_JSON_H
#define WORKBENCH_JSON_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct Json {
	enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
	bool boolean = false;
	double number = 0.0;
	std::string text;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> members;

	/* Offsets of the value in the text that was parsed: what lets the writer
	 * replace one literal and keep every comment and every space around it. */
	std::size_t start = 0;
	std::size_t end = 0;

	const Json* member(const std::string& key) const;
	const Json* member(const char* key) const { return member(std::string(key)); }
};

/** Parses a JSONC document. On failure `error` says where. */
bool json_parse(const std::string& text, Json& root, std::string& error);

#endif
