"""A small JSONC value and reader, shared by the settings and the schema.

Supports objects, arrays, strings with the usual escapes, numbers, true/false/
null, ``//`` and block comments, and trailing commas - what a hand-edited
configuration file and a JSON Schema both need.

Every value records the span it occupied in the text, which is what lets the
writer replace one literal and keep every comment and every space around it.
"""


class Json:
    NULL = "null"
    BOOL = "bool"
    NUMBER = "number"
    STRING = "string"
    ARRAY = "array"
    OBJECT = "object"

    def __init__(self):
        self.kind = Json.NULL
        self.boolean = False
        self.number = 0.0
        self.text = ""
        self.items = []
        self.members = []
        self.start = 0
        self.end = 0

    def member(self, key):
        if self.kind != Json.OBJECT:
            return None
        for name, value in self.members:
            if name == key:
                return value
        return None


class _Parser:
    def __init__(self, text):
        self.source = text
        self.index = 0
        self.error = ""

    def at_end(self):
        return self.index >= len(self.source)

    def peek(self):
        return "" if self.at_end() else self.source[self.index]

    def fail(self, message):
        if self.error:
            return
        line = self.source[: self.index].count("\n") + 1
        self.error = "line %d: %s" % (line, message)

    def skip_space(self):
        while True:
            while not self.at_end() and self.source[self.index].isspace():
                self.index += 1
            if self.source.startswith("//", self.index):
                self.index += 2
                while not self.at_end() and self.peek() != "\n":
                    self.index += 1
                continue
            if self.source.startswith("/*", self.index):
                self.index += 2
                close = self.source.find("*/", self.index)
                if close < 0:
                    self.fail("unterminated block comment")
                    return
                self.index = close + 2
                continue
            return

    def parse_string(self):
        if self.peek() != '"':
            self.fail("expected a string")
            return None
        self.index += 1
        out = []
        while not self.at_end() and self.peek() != '"':
            c = self.source[self.index]
            self.index += 1
            if c != "\\":
                out.append(c)
                continue
            if self.at_end():
                self.fail("unterminated escape")
                return None
            escape = self.source[self.index]
            self.index += 1
            simple = {'"': '"', "\\": "\\", "/": "/", "b": "\b", "f": "\f",
                      "n": "\n", "r": "\r", "t": "\t"}
            if escape in simple:
                out.append(simple[escape])
            elif escape == "u":
                if self.index + 4 > len(self.source):
                    self.fail("short \\u escape")
                    return None
                digits = self.source[self.index:self.index + 4]
                try:
                    out.append(chr(int(digits, 16)))
                except ValueError:
                    self.fail("bad \\u escape")
                    return None
                self.index += 4
            else:
                self.fail("unknown escape")
                return None
        if self.at_end():
            self.fail("unterminated string")
            return None
        self.index += 1
        return "".join(out)

    def parse_number(self):
        start = self.index
        if not self.at_end() and self.peek() in "+-":
            self.index += 1
        while not self.at_end() and self.peek() in "0123456789.eE+-":
            self.index += 1
        try:
            return float(self.source[start:self.index])
        except ValueError:
            self.fail("expected a number")
            return None

    def parse_value(self, depth):
        self.skip_space()
        start = self.index
        value = self.parse_value_inner(depth)
        if value is None:
            return None
        value.start = start
        value.end = self.index
        return value

    def parse_value_inner(self, depth):
        if depth > 64:
            self.fail("too deeply nested")
            return None
        self.skip_space()
        if self.at_end():
            self.fail("unexpected end of file")
            return None
        out = Json()
        c = self.peek()
        if c == "{":
            self.index += 1
            out.kind = Json.OBJECT
            self.skip_space()
            if self.peek() == "}":
                self.index += 1
                return out
            while True:
                self.skip_space()
                if self.peek() == "}":  # trailing comma
                    self.index += 1
                    return out
                key = self.parse_string()
                if key is None:
                    return None
                self.skip_space()
                if self.peek() != ":":
                    self.fail("expected ':' after a key")
                    return None
                self.index += 1
                value = self.parse_value(depth + 1)
                if value is None:
                    return None
                out.members.append((key, value))
                self.skip_space()
                if self.peek() == ",":
                    self.index += 1
                    continue
                if self.peek() == "}":
                    self.index += 1
                    return out
                self.fail("expected ',' or '}' in an object")
                return None
        if c == "[":
            self.index += 1
            out.kind = Json.ARRAY
            self.skip_space()
            if self.peek() == "]":
                self.index += 1
                return out
            while True:
                value = self.parse_value(depth + 1)
                if value is None:
                    return None
                out.items.append(value)
                self.skip_space()
                if self.peek() == ",":
                    self.index += 1
                    continue
                if self.peek() == "]":
                    self.index += 1
                    return out
                self.fail("expected ',' or ']' in an array")
                return None
        if c == '"':
            out.kind = Json.STRING
            out.text = self.parse_string()
            return out if out.text is not None else None
        if self.source.startswith("true", self.index):
            self.index += 4
            out.kind, out.boolean = Json.BOOL, True
            return out
        if self.source.startswith("false", self.index):
            self.index += 5
            out.kind, out.boolean = Json.BOOL, False
            return out
        if self.source.startswith("null", self.index):
            self.index += 4
            return out
        if c in "-0123456789":
            out.kind = Json.NUMBER
            number = self.parse_number()
            if number is None:
                return None
            out.number = number
            return out
        self.fail("unexpected character")
        return None


def parse(text):
    """Parses a JSONC document; raises ValueError with the line on failure."""
    parser = _Parser(text)
    root = parser.parse_value(0)
    if root is None:
        raise ValueError(parser.error or "invalid document")
    parser.skip_space()
    if not parser.at_end():
        raise ValueError(parser.error or "trailing text after the root value")
    return root
