/* Minimal assertion helpers for the host tests. */
#pragma once

#include <stdio.h>

static int check_failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            check_failures++;                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

#define CHECK_EQ(a, b) CHECK((long)(a) == (long)(b), "%s = %ld, want %ld", #a, (long)(a), (long)(b))

static int check_done(const char *name) {
    if (check_failures) {
        printf("%s: %d check(s) failed\n", name, check_failures);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}
