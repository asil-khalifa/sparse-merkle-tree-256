#include <chrono>
#include <cstring>
#include <iostream>

#include "test_framework.hpp"

std::vector<TestCase>& test_registry() {
  static std::vector<TestCase> r;
  return r;
}

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int passed = 0, failed = 0;
  std::cout << "Running " << test_registry().size() << " test(s)\n";
  for (const TestCase& t : test_registry()) {
    if (filter && std::strstr(t.name, filter) == nullptr) continue;
    const auto t0 = std::chrono::steady_clock::now();
    try {
      t.fn();
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::cout << "[PASS] " << t.name << " (" << ms << " ms)\n";
      ++passed;
    } catch (const std::exception& e) {
      std::cout << "[FAIL] " << t.name << "\n       " << e.what() << "\n";
      ++failed;
    }
  }
  std::cout << "\n" << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}
