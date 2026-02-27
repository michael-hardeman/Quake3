#define NOB_IMPLEMENTATION
#include "nob.h"

#define SRC   "."
#define BUILD "build/"
#define SPVD  BUILD "shaders/"
#define MAIN  SRC "quake3.c"

/* ── shader extraction ────────────────────────────────────────── */

/* Embedded shaders can appear anywhere in quake3.c at the top level
   (outside of any C brace-delimited block) as:

       glsl shader <Name> <stage> {
       ...GLSL source...
       }

   nob walks the file tracking C brace depth.  A `glsl shader` line
   at depth 0 is recognised as a shader block; anything nested inside
   a C function, struct, etc. is ignored.  The C-only output written
   to build/quake3.c has every shader block excised; each block's
   GLSL body is extracted to its own .glsl file for SPIR-V compilation. */

typedef struct {
    char        name[64];
    char        stage[16];
    const char *code;
    size_t      code_len;
    const char *block_start; /* first char of `glsl shader ...` line  */
    const char *block_end;   /* first char after the closing `}\n`    */
} Shader_Block;

#define MAX_SHADERS 16

/* Find the start of the line that contains `pos`. */
static const char *line_start(const char *src, const char *pos) {
    const char *ls = pos;
    while (ls > src && ls[-1] != '\n') ls--;
    return ls;
}

/* Walk the source tracking C brace depth and extract top-level
   `glsl shader` blocks.  Returns the number of blocks found. */
static int extract_shaders(const char *src, Shader_Block *out, int max) {
    int count = 0;
    int depth = 0;           /* C brace nesting depth */
    const char *p = src;

    while (*p && count < max) {
        /* skip C line comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        /* skip C block comments */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        /* skip string literals */
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\') p++; p++; }
            if (*p) p++;
            continue;
        }
        /* skip character literals */
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'') { if (*p == '\\') p++; p++; }
            if (*p) p++;
            continue;
        }

        /* track C brace depth */
        if (*p == '{' && depth >= 0) { depth++; p++; continue; }
        if (*p == '}' && depth >  0) { depth--; p++; continue; }

        /* look for `glsl shader ` at depth 0, at the start of a line */
        if (depth == 0 && strncmp(p, "glsl shader ", 12) == 0
            && (p == src || p[-1] == '\n')) {

            const char *blk = p;         /* remember block start */
            p += 12;

            /* name */
            const char *ns = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            size_t nl = (size_t)(p - ns);
            if (nl >= sizeof out[0].name) nl = sizeof out[0].name - 1;
            memcpy(out[count].name, ns, nl);
            out[count].name[nl] = '\0';

            while (*p == ' ') p++;

            /* stage */
            const char *ss = p;
            while (*p && *p != ' ' && *p != '{' && *p != '\n') p++;
            size_t sl = (size_t)(p - ss);
            while (sl > 0 && ss[sl - 1] == ' ') sl--;
            if (sl >= sizeof out[0].stage) sl = sizeof out[0].stage - 1;
            memcpy(out[count].stage, ss, sl);
            out[count].stage[sl] = '\0';

            /* opening brace */
            while (*p && *p != '{') p++;
            if (!*p) break;
            p++;
            if (*p == '\n') p++;

            /* shader body — brace-count to find the matching close */
            const char *code_start = p;
            const char *code_end   = NULL;
            int sdepth = 1;
            while (*p && sdepth > 0) {
                if      (*p == '{') sdepth++;
                else if (*p == '}') { if (--sdepth == 0) { code_end = p; break; } }
                p++;
            }
            if (!code_end) break;

            /* skip past the closing brace and trailing newline */
            p = code_end + 1;
            if (*p == '\n') p++;

            out[count].code        = code_start;
            out[count].code_len    = (size_t)(code_end - code_start);
            out[count].block_start = blk;
            out[count].block_end   = p;
            count++;
            continue;
        }

        p++;
    }
    return count;
}

/* Write the C-only source: copy `src` but skip every shader block region. */
static bool write_c_source(const char *path, const char *src, size_t src_len,
                            const Shader_Block *sh, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) { nob_log(NOB_ERROR, "could not open %s for writing", path); return false; }

    const char *cursor = src;
    const char *end    = src + src_len;
    for (int i = 0; i < n; i++) {
        /* write everything between cursor and the start of this block */
        if (sh[i].block_start > cursor)
            fwrite(cursor, 1, (size_t)(sh[i].block_start - cursor), f);
        cursor = sh[i].block_end;
    }
    /* write the remainder after the last block */
    if (cursor < end)
        fwrite(cursor, 1, (size_t)(end - cursor), f);

    fclose(f);
    return true;
}

/* ── helpers ──────────────────────────────────────────────────── */

static bool write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { nob_log(NOB_ERROR, "could not open %s for writing", path); return false; }
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

static bool spv_stale(const char *spv, const char *src) {
    return !nob_file_exists(spv) || nob_needs_rebuild1(spv, src);
}

/* ── main ─────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (!nob_mkdir_if_not_exists(BUILD)) return 1;
    if (!nob_mkdir_if_not_exists(SPVD))  return 1;

    /* Read the single-source file */
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(MAIN, &sb)) return 1;
    nob_sb_append_null(&sb);

    /* Extract top-level shader blocks */
    Shader_Block shaders[MAX_SHADERS];
    int n = extract_shaders(sb.items, shaders, MAX_SHADERS);
    nob_log(NOB_INFO, "extracted %d shader(s) from %s", n, MAIN);

    /* Write the C-only source (shader blocks excised) */
    if (!write_c_source(BUILD "quake3.c", sb.items, sb.count - 1, shaders, n)) return 1;

    /* Write all extracted shaders to build/shaders/ */
    char glsl_paths[MAX_SHADERS][256], spv_paths[MAX_SHADERS][256];
    for (int i = 0; i < n; i++) {
        snprintf(glsl_paths[i], sizeof glsl_paths[i], SPVD "%s.glsl", shaders[i].name);
        snprintf(spv_paths[i],  sizeof spv_paths[i],  SPVD "%s.spv",  shaders[i].name);
        if (!write_file(glsl_paths[i], shaders[i].code, shaders[i].code_len)) return 1;
    }

    /* Compile each shader to SPIR-V */
    Nob_Cmd cmd = {0};
    for (int i = 0; i < n; i++) {
        if (!spv_stale(spv_paths[i], MAIN)) continue;
        nob_cmd_append(&cmd, "glslangValidator",
            "-V", "--target-env", "vulkan1.3",
            "-S", shaders[i].stage,
            "-o", spv_paths[i], glsl_paths[i]);
        if (!nob_cmd_run(&cmd)) return 1;
    }

    /* Compile the C portion */
    nob_cmd_append(&cmd,
        "cc", "-O2", "-g", "-Wall", "-Wextra",
        "-o", BUILD "quake3",
        BUILD "quake3.c",
        "-I/usr/include/SDL2", "-D_REENTRANT",
        "-lSDL2", "-lvulkan", "-lm");
    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
