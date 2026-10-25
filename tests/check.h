// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <sstream>

inline int g_checks = 0;
inline int g_failures = 0;

inline void checkTrue(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, expr);
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* expr, const char* file, int line) {
    ++g_checks;
    if (a == b) return;
    ++g_failures;
    std::ostringstream os;
    os << "[" << a << "] != [" << b << "]";
    std::fprintf(stderr, "%s:%d: CHECK_EQ(%s) failed: %s\n", file, line, expr, os.str().c_str());
}

#define CHECK(...) checkTrue((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)
#define CHECK_EQ(a, b) checkEq((a), (b), #a ", " #b, __FILE__, __LINE__)

inline int checkReport() {
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
