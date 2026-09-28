// Minimal test harness: TEST(name) { CHECK(...); }  Run: vats_tests [filter]
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Register {
    Register(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};
inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, what.c_str());
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                                        \
    static void CHECK_CAT(test_, name)();                                                 \
    static check::Register CHECK_CAT(reg_, name)(#name, CHECK_CAT(test_, name));          \
    static void CHECK_CAT(test_, name)()

#define CHECK(cond) \
    do { if (!(cond)) check::fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        double va_ = (a), vb_ = (b);                                                             \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                                    \
            check::fail(__FILE__, __LINE__,                                                      \
                        std::string(#a " = ") + std::to_string(va_) + ", expected " + std::to_string(vb_)); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        auto va_ = (a);                                                                                   \
        auto vb_ = (b);                                                                                   \
        if (!(va_ == vb_)) check::fail(__FILE__, __LINE__, std::string(#a " == " #b));                    \
    } while (0)
