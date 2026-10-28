// Minimal JSON reader/writer (enough for proof files and CLI output).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace smt::json {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> obj;

  const Value* get(std::string_view key) const {
    for (const auto& kv : obj)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
  bool is_string() const { return type == Type::String; }
  bool is_number() const { return type == Type::Number; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }
};

bool parse(std::string_view text, Value& out, std::string* err = nullptr);
std::string escape(std::string_view s);

}  // namespace smt::json
