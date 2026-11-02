//! A small JSONC value and reader, shared by the settings and the schema.
//!
//! Supports objects, arrays, strings with the usual escapes, numbers,
//! true/false/null, `//` and block comments, and trailing commas - what a
//! hand-edited configuration file and a JSON Schema both need.
//!
//! Every value records the byte span it occupied, which is what lets the
//! writer replace one literal and keep every comment and every space around it.

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum Kind {
    #[default]
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

#[derive(Clone, Debug, Default)]
pub struct Json {
    pub kind: Kind,
    pub boolean: bool,
    pub number: f64,
    pub text: String,
    pub items: Vec<Json>,
    pub members: Vec<(String, Json)>,
    pub start: usize,
    pub end: usize,
}

impl Json {
    pub fn member(&self, key: &str) -> Option<&Json> {
        if self.kind != Kind::Object {
            return None;
        }
        self.members.iter().find(|(name, _)| name == key).map(|(_, value)| value)
    }
}

struct Parser<'a> {
    source: &'a [u8],
    index: usize,
    error: String,
}

impl<'a> Parser<'a> {
    fn new(text: &'a str) -> Self {
        Self { source: text.as_bytes(), index: 0, error: String::new() }
    }

    fn at_end(&self) -> bool {
        self.index >= self.source.len()
    }

    fn peek(&self) -> u8 {
        if self.at_end() { 0 } else { self.source[self.index] }
    }

    fn fail(&mut self, message: &str) {
        if !self.error.is_empty() {
            return;
        }
        let line = self.source[..self.index.min(self.source.len())]
            .iter()
            .filter(|&&c| c == b'\n')
            .count() + 1;
        self.error = format!("line {}: {}", line, message);
    }

    /// Whitespace and comments are skipped between every token.
    fn skip_space(&mut self) {
        loop {
            while !self.at_end() && (self.source[self.index] as char).is_ascii_whitespace() {
                self.index += 1;
            }
            if self.index + 1 < self.source.len()
                && self.source[self.index] == b'/'
                && self.source[self.index + 1] == b'/'
            {
                self.index += 2;
                while !self.at_end() && self.peek() != b'\n' {
                    self.index += 1;
                }
                continue;
            }
            if self.index + 1 < self.source.len()
                && self.source[self.index] == b'/'
                && self.source[self.index + 1] == b'*'
            {
                self.index += 2;
                while self.index + 1 < self.source.len()
                    && !(self.source[self.index] == b'*' && self.source[self.index + 1] == b'/')
                {
                    self.index += 1;
                }
                if self.index + 1 >= self.source.len() {
                    self.fail("unterminated block comment");
                    return;
                }
                self.index += 2;
                continue;
            }
            return;
        }
    }

    fn parse_string(&mut self) -> Option<String> {
        if self.peek() != b'"' {
            self.fail("expected a string");
            return None;
        }
        self.index += 1;
        let mut out = String::new();
        while !self.at_end() && self.peek() != b'"' {
            let mut c = self.source[self.index];
            self.index += 1;
            if c == b'\\' {
                if self.at_end() {
                    self.fail("unterminated escape");
                    return None;
                }
                let escape = self.source[self.index];
                self.index += 1;
                match escape {
                    b'"' => out.push('"'),
                    b'\\' => out.push('\\'),
                    b'/' => out.push('/'),
                    b'b' => out.push('\u{8}'),
                    b'f' => out.push('\u{c}'),
                    b'n' => out.push('\n'),
                    b'r' => out.push('\r'),
                    b't' => out.push('\t'),
                    b'u' => {
                        if self.index + 4 > self.source.len() {
                            self.fail("short \\u escape");
                            return None;
                        }
                        let mut code = 0u32;
                        for _ in 0..4 {
                            let digit = self.source[self.index];
                            self.index += 1;
                            code <<= 4;
                            match digit {
                                b'0'..=b'9' => code |= (digit - b'0') as u32,
                                b'a'..=b'f' => code |= (digit - b'a' + 10) as u32,
                                b'A'..=b'F' => code |= (digit - b'A' + 10) as u32,
                                _ => {
                                    self.fail("bad \\u escape");
                                    return None;
                                }
                            }
                        }
                        out.push(char::from_u32(code).unwrap_or('\u{fffd}'));
                    }
                    _ => {
                        self.fail("unknown escape");
                        return None;
                    }
                }
                continue;
            }
            if c >= 0x80 {
                // Copy the whole UTF-8 sequence, not one byte of it.
                let start = self.index - 1;
                let width = match c {
                    0xC0..=0xDF => 2,
                    0xE0..=0xEF => 3,
                    0xF0..=0xF7 => 4,
                    _ => 1,
                };
                c = self.source[start];
                let end = (start + width).min(self.source.len());
                if let Ok(piece) = std::str::from_utf8(&self.source[start..end]) {
                    out.push_str(piece);
                    self.index = end;
                    continue;
                }
            }
            out.push(c as char);
        }
        if self.at_end() {
            self.fail("unterminated string");
            return None;
        }
        self.index += 1;
        Some(out)
    }

    fn parse_number(&mut self) -> Option<f64> {
        let start = self.index;
        if !self.at_end() && (self.peek() == b'-' || self.peek() == b'+') {
            self.index += 1;
        }
        while !self.at_end()
            && matches!(self.peek(), b'0'..=b'9' | b'.' | b'e' | b'E' | b'+' | b'-')
        {
            self.index += 1;
        }
        let text = std::str::from_utf8(&self.source[start..self.index]).ok()?;
        match text.parse::<f64>() {
            Ok(number) => Some(number),
            Err(_) => {
                self.fail("expected a number");
                None
            }
        }
    }

    fn parse_value(&mut self, depth: usize) -> Option<Json> {
        self.skip_space();
        let start = self.index;
        let mut out = self.parse_value_inner(depth)?;
        out.start = start;
        out.end = self.index;
        Some(out)
    }

    fn parse_value_inner(&mut self, depth: usize) -> Option<Json> {
        if depth > 64 {
            self.fail("too deeply nested");
            return None;
        }
        self.skip_space();
        if self.at_end() {
            self.fail("unexpected end of file");
            return None;
        }
        let mut out = Json::default();
        match self.peek() {
            b'{' => {
                self.index += 1;
                out.kind = Kind::Object;
                self.skip_space();
                if self.peek() == b'}' {
                    self.index += 1;
                    return Some(out);
                }
                loop {
                    self.skip_space();
                    if self.peek() == b'}' {
                        self.index += 1; // trailing comma
                        return Some(out);
                    }
                    let key = self.parse_string()?;
                    self.skip_space();
                    if self.peek() != b':' {
                        self.fail("expected ':' after a key");
                        return None;
                    }
                    self.index += 1;
                    let value = self.parse_value(depth + 1)?;
                    out.members.push((key, value));
                    self.skip_space();
                    if self.peek() == b',' {
                        self.index += 1;
                        continue;
                    }
                    if self.peek() == b'}' {
                        self.index += 1;
                        return Some(out);
                    }
                    self.fail("expected ',' or '}' in an object");
                    return None;
                }
            }
            b'[' => {
                self.index += 1;
                out.kind = Kind::Array;
                self.skip_space();
                if self.peek() == b']' {
                    self.index += 1;
                    return Some(out);
                }
                loop {
                    out.items.push(self.parse_value(depth + 1)?);
                    self.skip_space();
                    if self.peek() == b',' {
                        self.index += 1;
                        continue;
                    }
                    if self.peek() == b']' {
                        self.index += 1;
                        return Some(out);
                    }
                    self.fail("expected ',' or ']' in an array");
                    return None;
                }
            }
            b'"' => {
                out.kind = Kind::String;
                out.text = self.parse_string()?;
                Some(out)
            }
            b't' if self.source[self.index..].starts_with(b"true") => {
                self.index += 4;
                out.kind = Kind::Bool;
                out.boolean = true;
                Some(out)
            }
            b'f' if self.source[self.index..].starts_with(b"false") => {
                self.index += 5;
                out.kind = Kind::Bool;
                out.boolean = false;
                Some(out)
            }
            b'n' if self.source[self.index..].starts_with(b"null") => {
                self.index += 4;
                Some(out)
            }
            b'-' | b'0'..=b'9' => {
                out.kind = Kind::Number;
                out.number = self.parse_number()?;
                Some(out)
            }
            _ => {
                self.fail("unexpected character");
                None
            }
        }
    }
}

/// Parses a JSONC document. On failure the error says where.
pub fn parse(text: &str) -> Result<Json, String> {
    let mut parser = Parser::new(text);
    let root = parser.parse_value(0);
    if root.is_none() {
        return Err(if parser.error.is_empty() { "invalid document".into() } else { parser.error });
    }
    parser.skip_space();
    if !parser.at_end() {
        return Err(if parser.error.is_empty() {
            "trailing text after the root value".into()
        } else {
            parser.error
        });
    }
    Ok(root.unwrap())
}
