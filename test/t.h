#pragma once
// Minimal host test macros. Each test_*.c defines `void test_<name>(void)`
// functions; test_main.c runs them with RUN().
#include <stdio.h>
#include <string.h>

extern int t_pass, t_fail;

#define T_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { t_pass++; } else { t_fail++; \
      printf("FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, _a, _b); } \
  } while (0)

#define T_TRUE(c) do { \
    if (c) { t_pass++; } else { t_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
  } while (0)

#define T_STR(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (strcmp(_a, _b) == 0) { t_pass++; } else { t_fail++; \
      printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a, _b); } \
  } while (0)

#define RUN(fn) do { void fn(void); fn(); } while (0)
