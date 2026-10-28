#include "smt/json.hpp"

#include <cctype>
#include <cstdlib>

namespace smt::json {

namespace {

struct Parser {
  std::string_view s;
  std::size_t i = 0;
  std::string err;

  void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool fail(const char* m) { if (err.empty()) err = m; return false; }

  bool parse_string(std::string& out) {
    if (i >= s.size() || s[i] != '"') return fail("expected string");
    ++i;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return true;
      if (c == '\\') {
        if (i >= s.size()) return fail("bad escape");
        char e = s[i++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (i + 4 > s.size()) return fail("bad \\u escape");
            unsigned cp = std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16);
            i += 4;
            if (cp < 0x80) out += char(cp);
            else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
            else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            break;
          }
          default: return fail("bad escape");
        }
      } else {
        out += c;
      }
    }
    return fail("unterminated string");
  }

  bool parse_value(Value& v) {
    ws();
    if (i >= s.size()) return fail("unexpected end");
    char c = s[i];
    if (c == '{') {
      ++i;
      v.type = Value::Type::Object;
      ws();
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      while (true) {
        ws();
        std::string key;
        if (!parse_string(key)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        ++i;
        Value child;
        if (!parse_value(child)) return false;
        v.obj.emplace_back(std::move(key), std::move(child));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++i;
      v.type = Value::Type::Array;
      ws();
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      while (true) {
        Value child;
        if (!parse_value(child)) return false;
        v.arr.push_back(std::move(child));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      v.type = Value::Type::String;
      return parse_string(v.str);
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; v.type = Value::Type::Bool; v.b = true; return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; v.type = Value::Type::Bool; v.b = false; return true; }
    if (s.compare(i, 4, "null") == 0) { i += 4; v.type = Value::Type::Null; return true; }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t start = i;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                              s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
        ++i;
      v.type = Value::Type::Number;
      v.num = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
      return true;
    }
    return fail("unexpected character");
  }
};

}  // namespace

bool parse(std::string_view text, Value& out, std::string* err) {
  Parser p{text, 0, {}};
  out = Value{};
  if (!p.parse_value(out)) {
    if (err) *err = "JSON parse error: " + p.err;
    return false;
  }
  p.ws();
  if (p.i != text.size()) {
    if (err) *err = "JSON parse error: trailing characters";
    return false;
  }
  return true;
}

std::string escape(std::string_view s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          o += buf;
        } else {
          o += c;
        }
    }
  }
  return o;
}

}  // namespace smt::json
