#include <cstdio>

int g_tu2_val = 0;

struct Tu2Init {
    Tu2Init() {
        ++g_tu2_val;
    }
};

static Tu2Init s_tu2_init;
