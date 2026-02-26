#define NOB_IMPLEMENTATION
#include "nob.h"

#define SRC   "src/"
#define BUILD "build/"
#define SPVD  BUILD "shaders/"

typedef struct { const char *name, *stage; } Glsl;
static const Glsl GLSL[] = {
    {"rgen",         "rgen"},
    {"rchit",        "rchit"},
    {"rmiss",        "rmiss"},
    {"shadow.rmiss", "rmiss"},
    {NULL, NULL},
};

static int spv_stale(const char *spv, const char *glsl) {
    return !nob_file_exists(spv) || nob_needs_rebuild1(spv, glsl);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (!nob_mkdir_if_not_exists(BUILD)) return 1;
    if (!nob_mkdir_if_not_exists(SPVD))  return 1;

    Nob_Cmd cmd = {0};

    for (int i = 0; GLSL[i].name; i++) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), SRC "%s.glsl",  GLSL[i].name);
        snprintf(dst, sizeof(dst), SPVD "%s.spv",  GLSL[i].name);
        if (!spv_stale(dst, src)) continue;
        nob_cmd_append(&cmd, "glslangValidator",
            "-V", "--target-env", "vulkan1.3",
            "-S", GLSL[i].stage,
            "-o", dst, src);
        if (!nob_cmd_run(&cmd)) return 1;
    }

    nob_cmd_append(&cmd,
        "cc", "-O2", "-g", "-Wall", "-Wextra",
        "-o", BUILD "quake3",
        SRC "quake3.c",
        "-I/usr/include/SDL2", "-D_REENTRANT",
        "-lSDL2", "-lvulkan", "-lm");
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
