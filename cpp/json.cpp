/**
 * The JSONC reader - see json.h.
 */

#include "json.h"

#include <cctype>
#include <cstdlib>

const Json* Json::member(const std::string& key) const {
	if (kind != Object) return nullptr;
	for (const auto& entry : members) {
		if (entry.first == key) return &entry.second;
	}
	return nullptr;
}

namespace {

struct Parser {
	const std::string& source;
	size_t index = 0;
	std::string error;

	explicit Parser(const std::string& text) : source(text) {}

	bool at_end() const { return index >= source.size(); }
	char peek() const { return at_end() ? '\0' : source[index]; }

	void fail(const std::string& message) {
		if (!error.empty()) return;
		int line = 1;
		for (size_t i = 0; i < index && i < source.size(); i++) {
			if (source[i] == '\n') line++;
		}
		error = "line " + std::to_string(line) + ": " + message;
	}

	/** Whitespace and comments are skipped between every token. */
	void skip_space() {
		for (;;) {
			while (!at_end() && std::isspace((unsigned char)peek())) index++;
			if (index + 1 < source.size() && source[index] == '/' && source[index + 1] == '/') {
				index += 2;
				while (!at_end() && peek() != '\n') index++;
				continue;
			}
			if (index + 1 < source.size() && source[index] == '/' && source[index + 1] == '*') {
				index += 2;
				while (index + 1 < source.size() && !(source[index] == '*' && source[index + 1] == '/')) index++;
				if (index + 1 >= source.size()) {
					fail("unterminated block comment");
					return;
				}
				index += 2;
				continue;
			}
			return;
		}
	}

	bool parse_string(std::string& out) {
		if (peek() != '"') {
			fail("expected a string");
			return false;
		}
		index++;
		while (!at_end() && peek() != '"') {
			char c = source[index++];
			if (c == '\\') {
				if (at_end()) {
					fail("unterminated escape");
					return false;
				}
				char escape = source[index++];
				switch (escape) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					if (index + 4 > source.size()) {
						fail("short \\u escape");
						return false;
					}
					unsigned code = 0;
					for (int i = 0; i < 4; i++) {
						char digit = source[index++];
						code <<= 4;
						if (digit >= '0' && digit <= '9') code |= (unsigned)(digit - '0');
						else if (digit >= 'a' && digit <= 'f') code |= (unsigned)(digit - 'a' + 10);
						else if (digit >= 'A' && digit <= 'F') code |= (unsigned)(digit - 'A' + 10);
						else {
							fail("bad \\u escape");
							return false;
						}
					}
					if (code < 0x80) {
						out += (char)code;
					} else if (code < 0x800) {
						out += (char)(0xC0 | (code >> 6));
						out += (char)(0x80 | (code & 0x3F));
					} else {
						out += (char)(0xE0 | (code >> 12));
						out += (char)(0x80 | ((code >> 6) & 0x3F));
						out += (char)(0x80 | (code & 0x3F));
					}
					break;
				}
				default:
					fail("unknown escape");
					return false;
				}
				continue;
			}
			out += c;
		}
		if (at_end()) {
			fail("unterminated string");
			return false;
		}
		index++;
		return true;
	}

	bool parse_number(Json& out) {
		const char* start = source.c_str() + index;
		char* end = nullptr;
		double number = std::strtod(start, &end);
		if (end == start) {
			fail("expected a number");
			return false;
		}
		index += (size_t)(end - start);
		out.kind = Json::Number;
		out.number = number;
		return true;
	}

	bool parse_value(Json& out, int depth) {
		skip_space();
		out.start = index;
		if (!parse_value_inner(out, depth)) return false;
		out.end = index;
		return true;
	}

	bool parse_value_inner(Json& out, int depth) {
		if (depth > 64) {
			fail("too deeply nested");
			return false;
		}
		skip_space();
		if (at_end()) {
			fail("unexpected end of file");
			return false;
		}

		char c = peek();
		if (c == '{') {
			index++;
			out.kind = Json::Object;
			skip_space();
			if (peek() == '}') {
				index++;
				return true;
			}
			for (;;) {
				skip_space();
				if (peek() == '}') {  /* trailing comma */
					index++;
					return true;
				}
				std::string key;
				if (!parse_string(key)) return false;
				skip_space();
				if (peek() != ':') {
					fail("expected ':' after a key");
					return false;
				}
				index++;
				Json value;
				if (!parse_value(value, depth + 1)) return false;
				out.members.emplace_back(key, std::move(value));
				skip_space();
				if (peek() == ',') {
					index++;
					continue;
				}
				if (peek() == '}') {
					index++;
					return true;
				}
				fail("expected ',' or '}' in an object");
				return false;
			}
		}
		if (c == '[') {
			index++;
			out.kind = Json::Array;
			skip_space();
			if (peek() == ']') {
				index++;
				return true;
			}
			for (;;) {
				Json value;
				if (!parse_value(value, depth + 1)) return false;
				out.items.push_back(std::move(value));
				skip_space();
				if (peek() == ',') {
					index++;
					continue;
				}
				if (peek() == ']') {
					index++;
					return true;
				}
				fail("expected ',' or ']' in an array");
				return false;
			}
		}
		if (c == '"') {
			out.kind = Json::String;
			return parse_string(out.text);
		}
		if (c == 't' && source.compare(index, 4, "true") == 0) {
			index += 4;
			out.kind = Json::Bool;
			out.boolean = true;
			return true;
		}
		if (c == 'f' && source.compare(index, 5, "false") == 0) {
			index += 5;
			out.kind = Json::Bool;
			out.boolean = false;
			return true;
		}
		if (c == 'n' && source.compare(index, 4, "null") == 0) {
			index += 4;
			out.kind = Json::Null;
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);

		fail("unexpected character");
		return false;
	}
};

}  // namespace

bool json_parse(const std::string& text, Json& root, std::string& error) {
	Parser parser(text);
	if (!parser.parse_value(root, 0)) {
		error = parser.error.empty() ? "invalid document" : parser.error;
		return false;
	}
	parser.skip_space();
	if (!parser.at_end()) {
		error = parser.error.empty() ? "trailing text after the root value" : parser.error;
		return false;
	}
	return true;
}
