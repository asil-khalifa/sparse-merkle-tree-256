// Tiny self-contained unit test framework (no external dependencies).
#pragma once
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TestFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct TestCase {
  const char* name;
  void (*fn)();
};
std::vector<TestCase>& test_registry();

struct TestRegistrar {
  TestRegistrar(const char* name, void (*fn)()) { test_registry().push_back({name, fn}); }
};

#define TEST(name)                                    \
  static void name();                                 \
  static TestRegistrar test_registrar_##name(#name, name); \
  static void name()

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::ostringstream _o;                                                          \
      _o << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond;                 \
      throw TestFailure(_o.str());                                                    \
    }                                                                                 \
  } while (0)

#define CHECK_MSG(cond, msg)                                                          \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::ostringstream _o;                                                          \
      _o << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond << " -- " << msg; \
      throw TestFailure(_o.str());                                                    \
    }                                                                                 \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define CHECK_THROWS(expr)                                                            \
  do {                                                                                \
    bool _thrown = false;                                                             \
    try { (void)(expr); } catch (const std::exception&) { _thrown = true; }           \
    if (!_thrown) {                                                                   \
      std::ostringstream _o;                                                          \
      _o << __FILE__ << ":" << __LINE__ << ": expected exception from: " #expr;       \
      throw TestFailure(_o.str());                                                    \
    }                                                                                 \
  } while (0)
