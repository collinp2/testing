#pragma once

/// Minimal test framework — no external dependencies required.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& getTests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failCount() {
    static int count = 0;
    return count;
}

#define TEST(name) \
    void test_##name(); \
    static bool reg_##name = (getTests().push_back({#name, test_##name}), true); \
    void test_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failCount(); return; \
    }} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a != _b) { \
        std::fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        ++failCount(); return; \
    }} while(0)

#define ASSERT_NEAR(a, b, tol) \
    do { double _a = (a); double _b = (b); if (std::fabs(_a - _b) > (tol)) { \
        std::fprintf(stderr, "  FAIL: %s:%d: |%f - %f| > %f\n", \
                     __FILE__, __LINE__, _a, _b, (double)(tol)); \
        ++failCount(); return; \
    }} while(0)

inline int runAllTests() {
    int passed = 0;
    for (auto& t : getTests()) {
        int before = failCount();
        t.func();
        if (failCount() == before) {
            std::printf("  PASS: %s\n", t.name.c_str());
            ++passed;
        } else {
            std::printf("  FAIL: %s\n", t.name.c_str());
        }
    }
    int total = static_cast<int>(getTests().size());
    std::printf("\n%d/%d tests passed\n", passed, total);
    return (failCount() > 0) ? 1 : 0;
}
