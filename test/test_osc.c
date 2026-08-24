#include <stdio.h>
#include "../osc.h"

int errs = 0;

static void check_oscmatch(
        char const * const pat, char const * const seg, bool const expected, char ** end) {
    if (oscmatch(pat, seg, end) != expected) {
        errs++;
        fprintf(stderr, "Path %s %s %s\n", pat, expected ? "didn't match" : "unexpectedly matched", seg);
    }
}

static void test_oscmatch_basic(void) {
    char const * const p = "/pat/segs/yeah";
    check_oscmatch(p, "pat", true, NULL);
    check_oscmatch(p, "path", false, NULL);
    check_oscmatch(p, "pa", false, NULL);
    check_oscmatch(p, "nope", false, NULL);
    char * end;
    check_oscmatch(p, "pat", true, &end);
    check_oscmatch(end, "nope", false, &end);
    check_oscmatch(end, "segs", true, &end);
    check_oscmatch(end, "yeah", true, &end);
    if (*end != '\0') {
        fprintf(stderr, "Not at the end");
        errs++;
    }
}

static void test_oscmatch_star(void) {
    check_oscmatch("/*", "pat", true, NULL);
    check_oscmatch("/p*", "pat", true, NULL);
    check_oscmatch("/pat*", "pat", true, NULL);
    check_oscmatch("/*t", "pat", true, NULL);
    check_oscmatch("/p*t", "pat", true, NULL);
    check_oscmatch("/*a*", "pat", true, NULL);
    check_oscmatch("/x*", "pat", false, NULL);
    check_oscmatch("/*x", "pat", false, NULL);
    check_oscmatch("/*x*", "pat", false, NULL);
}

static void test_oscmatch_qmark(void) {
    check_oscmatch("/?at", "pat", true, NULL);
    check_oscmatch("/p?t", "pet", true, NULL);
    check_oscmatch("/p?t", "pt", false, NULL);
}

static void test_oscmatch_sqbr(void) {
    char const * const p = "/p[ae]t";
    check_oscmatch(p, "pat", true, NULL);
    check_oscmatch(p, "pet", true, NULL);
    check_oscmatch(p, "pit", false, NULL);
    check_oscmatch(p, "pt", false, NULL);
}

static void test_oscmatch_crbr(void) {
    char const * const p = "/p{ar,en}t";
    check_oscmatch(p, "part", true, NULL);
    check_oscmatch(p, "pent", true, NULL);
    check_oscmatch(p, "pat", false, NULL);
    check_oscmatch(p, "prt", false, NULL);
    check_oscmatch(p, "pet", false, NULL);
    check_oscmatch(p, "pnt", false, NULL);
}

int main(void) {
    test_oscmatch_basic();
    test_oscmatch_star();
    test_oscmatch_qmark();
    test_oscmatch_sqbr();
    test_oscmatch_crbr();
    return errs;
}
