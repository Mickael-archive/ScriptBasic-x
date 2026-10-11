/*
 * headerer.c -- C23 replacement for headerer.pl
 *
 * Reads C sources and generates their header files, plus the ScriptBasic
 * module import (.bas) files, exactly like the Perl original did:
 *
 *   FILE:   name.c          source name (used to derive the header name)
 *   HEADER: name.h          explicit header file name
 *   BAS:    name.bas        also generate the module BASIC import file
 *   TO_HEADER:              copy the following lines, up to a lone
 *                           star-slash line, into the header file
 *   TO_BAS:                 same, into the .bas file
 *   =section name           BASIC name for the next besFUNCTION/besCOMMAND
 *   .function name          idem
 *   besFUNCTION(name)       declare a module function
 *   besCOMMAND(name)        declare a module command
 *   slash-star FUNCTION star-slash
 *                           copy the prototype, up to a lone ") {" line,
 *                           wrapped in FUNDEF/FEDNUF markers
 *
 * The output is byte identical to the Perl output, including its mixed
 * line endings: lines copied from the input keep their carriage return,
 * while text embedded in the generator itself is LF only (Perl normalises
 * the line endings of the script source, but not of the data it reads).
 *
 * Usage: headerer file.c [more.c ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------- */
/* small helpers                                                         */
/* -------------------------------------------------------------------- */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s)
{
    return s ? xstrndup(s, strlen(s)) : NULL;
}

static int ascii_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int ascii_word(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int ascii_upper(int c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }
static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/* -------------------------------------------------------------------- */
/* dynamic byte buffer                                                   */
/* -------------------------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b)
{
    b->cap = 64;
    b->len = 0;
    b->buf = xmalloc(b->cap);
    b->buf[0] = '\0';
}

static void buf_room(Buf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap)
            b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
        if (!b->buf) { fputs("out of memory\n", stderr); exit(1); }
    }
}

static void buf_add(Buf *b, const char *data, size_t n)
{
    if (n == 0) return;
    buf_room(b, n);
    memcpy(b->buf + b->len, data, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
static void buf_ch(Buf *b, char c) { buf_add(b, &c, 1); }

/* -------------------------------------------------------------------- */
/* input file split into lines: content without the final '\n',          */
/* the carriage return (CRLF files) is preserved, like Perl's chomp      */
/* -------------------------------------------------------------------- */

typedef struct { const char *s; size_t n; } Line;

typedef struct {
    char  *data;
    size_t size;
    Line  *lines;
    size_t count;
} Src;

static int src_read(const char *path, Src *f)
{
    FILE *fp = fopen(path, "rb");
    long sz;
    char *data;
    size_t i, start, count;

    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0) { fclose(fp); return -1; }
    rewind(fp);
    data = xmalloc((size_t)sz + 1);
    if (sz > 0 && fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(data);
        return -1;
    }
    fclose(fp);
    data[sz] = '\0';

    count = 0;
    for (i = 0; i < (size_t)sz; i++)
        if (data[i] == '\n') count++;
    if (sz > 0 && data[sz - 1] != '\n') count++;

    f->lines = xmalloc(sizeof(Line) * (count ? count : 1));
    f->data = data;
    f->size = (size_t)sz;
    f->count = 0;

    start = 0;
    for (i = 0; i < (size_t)sz; i++) {
        if (data[i] == '\n') {
            f->lines[f->count].s = data + start;
            f->lines[f->count].n = i - start;
            f->count++;
            start = i + 1;
        }
    }
    if (start < (size_t)sz) {
        f->lines[f->count].s = data + start;
        f->lines[f->count].n = (size_t)sz - start;
        f->count++;
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Perl regex equivalents, on one Line                                   */
/* -------------------------------------------------------------------- */

static size_t lead(const Line *l)
{
    size_t i = 0;
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    return i;
}

static int lit(const Line *l, size_t *pi, const char *word)
{
    size_t i = *pi;
    for (size_t k = 0; word[k]; k++) {
        if (i >= l->n || l->s[i] != word[k]) return 0;
        i++;
    }
    *pi = i;
    return 1;
}

static int at_end(const Line *l, size_t i)
{
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    return i >= l->n;
}

/* ^\s* WORD \s* :  (with an optional \S+ capture after the colon) */
static int match_key_colon(const Line *l, const char *word, size_t *after)
{
    size_t i = lead(l);
    if (!lit(l, &i, word)) return 0;
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    if (i >= l->n || l->s[i] != ':') return 0;
    *after = i + 1;
    return 1;
}

static int capture_nonspace(const Line *l, size_t i, const char **cap, size_t *len)
{
    size_t start;
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    start = i;
    while (i < l->n && !ascii_space((unsigned char)l->s[i])) i++;
    if (i == start) return 0;
    *cap = l->s + start;
    *len = i - start;
    return 1;
}

/* ^\s* WORD \s+ (\w+) \s*$   -- WORD is "=section" or ".function" */
static int match_eq_section(const Line *l, const char *word, const char **cap, size_t *len)
{
    size_t i = lead(l), start;
    if (!lit(l, &i, word)) return 0;
    if (i >= l->n || !ascii_space((unsigned char)l->s[i])) return 0;
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    start = i;
    while (i < l->n && ascii_word((unsigned char)l->s[i])) i++;
    if (i == start) return 0;
    *cap = l->s + start;
    *len = i - start;
    return at_end(l, i);
}

/* ^\s* WORD \( (\w+) \) \s*$   -- WORD is "besFUNCTION" or "besCOMMAND" */
static int match_bes(const Line *l, const char *word, const char **cap, size_t *len)
{
    size_t i = lead(l), start;
    if (!lit(l, &i, word)) return 0;
    if (i >= l->n || l->s[i] != '(') return 0;
    i++;
    start = i;
    while (i < l->n && ascii_word((unsigned char)l->s[i])) i++;
    if (i == start) return 0;
    *cap = l->s + start;
    *len = i - start;
    if (i >= l->n || l->s[i] != ')') return 0;
    return at_end(l, i + 1);
}

/* ^\s* WORD \s*$   -- e.g. "=POD", "=CUT" */
static int match_eq_kw_only(const Line *l, const char *word)
{
    size_t i = lead(l);
    return lit(l, &i, word) && at_end(l, i);
}

/* ^\s* star-slash \s*$ */
static int match_star_slash(const Line *l)
{
    size_t i = lead(l);
    return lit(l, &i, "*/") && at_end(l, i);
}

/* ^\s* ) \s* \{ \s*$ */
static int match_paren_brace(const Line *l)
{
    size_t i = lead(l);
    if (!lit(l, &i, ")")) return 0;
    while (i < l->n && ascii_space((unsigned char)l->s[i])) i++;
    if (!lit(l, &i, "{")) return 0;
    return at_end(l, i);
}

/* ^\s* slash-star FUNCTION star-slash \s*$ */
static int match_comment_function(const Line *l)
{
    size_t i = lead(l);
    return lit(l, &i, "/*FUNCTION*/") && at_end(l, i);
}

/* Perl: s{^(.*?)//(.*)}{$1} -- cut at the first double slash */
static size_t chop_slash_comment(const char *s, size_t n)
{
    for (size_t i = 0; i + 1 < n; i++)
        if (s[i] == '/' && s[i + 1] == '/') return i;
    return n;
}

/* Perl: s/\\\s*$/\\/ -- drop the whitespace that follows a final backslash */
static size_t chop_backslash_space(const char *s, size_t n)
{
    size_t i = n;
    while (i > 0 && ascii_space((unsigned char)s[i - 1])) i--;
    if (i > 0 && s[i - 1] == '\\') return i;
    return n;
}

/* -------------------------------------------------------------------- */
/* generation state for one input file                                   */
/* -------------------------------------------------------------------- */

typedef struct { char *basic, *cname; } Decl;

typedef struct {
    char *input_file_name;
    char *header_file_name;
    char *bas_file_name;
    char *module_name;
    char *basic_function_name;
    char *module_function_name;

    int   header_opened;
    int   bas_opened;
    char *open_header_name;
    char *open_bas_name;

    Buf   header;
    Buf   bas;

    Decl *subs; size_t nsubs, capsubs;
    Decl *coms; size_t ncoms, capcoms;
} Ctx;

static void set_name(char **slot, const char *val, size_t n)
{
    free(*slot);
    *slot = xstrndup(val, n);
}

/* Perl: s/\.\w+$/.h/ (only the last extension, \w+ must be non empty) */
static char *replace_ext_h(const char *name)
{
    size_t n = strlen(name), dot = 0;
    int found = 0;
    char *out;

    for (size_t i = n; i > 0; i--) {
        if (name[i - 1] == '.') { dot = i - 1; found = 1; break; }
    }
    if (found && dot + 1 < n) {
        size_t j = dot + 1;
        int ok = 1;
        for (; j < n; j++) {
            if (!ascii_word((unsigned char)name[j])) { ok = 0; break; }
        }
        if (ok) {
            out = xmalloc(dot + 3);
            memcpy(out, name, dot);
            out[dot] = '.';
            out[dot + 1] = 'h';
            out[dot + 2] = '\0';
            return out;
        }
    }
    return xstrdup(name);
}

/* the generated file goes into the source file's directory */
static char *join_dir(const char *path, const char *name)
{
    char *dir = xstrdup(path);
    char *slash;
    size_t len;
    char *out;

    for (char *p = dir; *p; p++)
        if (*p == '\\') *p = '/';
    slash = strrchr(dir, '/');
    if (!slash) { free(dir); return xstrdup(name); }
    *slash = '\0';
    len = strlen(dir) + 1 + strlen(name) + 1;
    out = xmalloc(len);
    snprintf(out, len, "%s/%s", dir, name);
    free(dir);
    return out;
}

/* __NAME_H__ from the header file name */
static char *header_symbol(const char *name)
{
    char *n = xstrdup(name);
    char *slash, *out;
    size_t len;

    for (char *p = n; *p; p++) *p = (char)ascii_upper((unsigned char)*p);
    slash = strrchr(n, '/');
    if (slash) memmove(n, slash + 1, strlen(slash + 1) + 1);
    for (char *p = n; *p; p++)
        if (*p == '.') *p = '_';
    len = strlen(n) + 5;
    out = xmalloc(len);
    snprintf(out, len, "__%s__", n);
    free(n);
    return out;
}

static char *module_name_from(const char *bas)
{
    char *m = xstrdup(bas);
    char *dot;

    for (char *p = m; *p; p++) *p = (char)ascii_lower((unsigned char)*p);
    dot = strchr(m, '.');
    if (dot && dot[1]) *dot = '\0';
    return m;
}

static void push_decl(Decl **arr, size_t *count, size_t *cap,
                      const char *basic, const char *cname)
{
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *arr = realloc(*arr, sizeof(Decl) * *cap);
        if (!*arr) { fputs("out of memory\n", stderr); exit(1); }
    }
    (*arr)[*count].basic = xstrdup(basic ? basic : "");
    (*arr)[*count].cname = xstrdup(cname ? cname : "");
    (*count)++;
}

static void open_header_file(Ctx *c, const char *path)
{
    char *sym;

    if (c->header_opened) return;
    c->header_opened = 1;

    if (!c->header_file_name && !c->input_file_name)
        c->header_file_name = replace_ext_h(path);
    if (!c->header_file_name)
        c->header_file_name = replace_ext_h(c->input_file_name ? c->input_file_name : "");
    if (!c->header_file_name || !c->header_file_name[0]) {
        fputs("No header file name.\n", stderr);
        exit(1);
    }
    c->open_header_name = join_dir(path, c->header_file_name);

    sym = header_symbol(c->header_file_name);
    buf_str(&c->header, "/*\n");
    buf_str(&c->header, c->header_file_name);
    buf_str(&c->header, "\n*/\n#ifndef ");
    buf_str(&c->header, sym);
    buf_str(&c->header, "\n#define ");
    buf_str(&c->header, sym);
    buf_str(&c->header, " 1\n#ifdef  __cplusplus\nextern \"C\" {\n#endif\n");
    free(sym);
}

static void open_bas_file(Ctx *c, const char *path)
{
    if (c->bas_opened) return;
    c->bas_opened = 1;
    if (!c->bas_file_name) return;
    c->open_bas_name = join_dir(path, c->bas_file_name);

    buf_str(&c->bas, "' \"\"\"\nFILE: ");
    buf_str(&c->bas, c->bas_file_name);
    buf_str(&c->bas, "\n\nThis is the BASIC import file for the module ");
    buf_str(&c->bas, c->module_name ? c->module_name : "");
    buf_str(&c->bas, ".\n\nThis file was generated by headerer.pl from the file ");
    buf_str(&c->bas, c->input_file_name ? c->input_file_name : "");
    buf_str(&c->bas, "\nDo not edit this file, rather edit the file ");
    buf_str(&c->bas, c->input_file_name ? c->input_file_name : "");
    buf_str(&c->bas, " and use\nheaderer.pl to regenerate this file.\n\"\"\"\n\nmodule ");
    buf_str(&c->bas, c->module_name ? c->module_name : "");
    buf_str(&c->bas, "\n\n");
}

static void write_if_different(const char *path, const Buf *content, const char *what)
{
    FILE *fp = fopen(path, "rb");

    if (fp) {
        long sz;
        char *data;
        int same = 0;

        if (fseek(fp, 0, SEEK_END) == 0 && (sz = ftell(fp)) >= 0) {
            rewind(fp);
            data = xmalloc((size_t)sz + 1);
            if (sz == 0 || fread(data, 1, (size_t)sz, fp) == (size_t)sz) {
                if ((size_t)sz == content->len &&
                    memcmp(data, content->buf, content->len) == 0)
                    same = 1;
            }
            free(data);
        }
        fclose(fp);
        if (same) return;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Can not open %s file %s\n", what, path);
        exit(1);
    }
    if (content->len)
        fwrite(content->buf, 1, content->len, fp);
    fclose(fp);
}

/* -------------------------------------------------------------------- */
/* the generator itself                                                  */
/* -------------------------------------------------------------------- */

static void process_file(const char *path)
{
    Src f;
    Ctx c;
    size_t i;
    Buf *tgt;

    if (src_read(path, &f) != 0) {
        fprintf(stderr, "Can not open the input file %s\n", path);
        exit(1);
    }

    memset(&c, 0, sizeof(c));
    buf_init(&c.header);
    buf_init(&c.bas);

    i = 0;
    while (i < f.count) {
        const Line *l = &f.lines[i];
        const char *cap;
        size_t clen, after;

        if (match_key_colon(l, "FILE", &after)) {
            if (capture_nonspace(l, after, &cap, &clen))
                set_name(&c.input_file_name, cap, clen);
            i++;
            continue;
        }

        if (match_key_colon(l, "HEADER", &after)) {
            if (capture_nonspace(l, after, &cap, &clen))
                set_name(&c.header_file_name, cap, clen);
            i++;
            continue;
        }

        if (match_key_colon(l, "BAS", &after)) {
            if (capture_nonspace(l, after, &cap, &clen)) {
                set_name(&c.bas_file_name, cap, clen);
                free(c.module_name);
                c.module_name = module_name_from(c.bas_file_name);
            }
            i++;
            continue;
        }

        if (match_key_colon(l, "TO_HEADER", &after)) {
            int podon = 0;
            open_header_file(&c, path);
            i++;
            tgt = &c.header;
            while (i < f.count) {
                const Line *t = &f.lines[i];
                const char *p;
                size_t n;
                if (match_eq_kw_only(t, "=POD")) { podon = 1; i++; continue; }
                if (match_eq_kw_only(t, "=CUT")) { podon = 0; i++; continue; }
                if (match_star_slash(t)) { i++; break; }
                p = t->s; n = t->n;
                n = chop_slash_comment(p, n);
                n = chop_backslash_space(p, n);
                if (!podon) {
                    buf_add(tgt, p, n);
                    buf_ch(tgt, '\n');
                }
                i++;
            }
            continue;
        }

        if (match_key_colon(l, "TO_BAS", &after)) {
            int podon = 0;
            open_bas_file(&c, path);
            i++;
            tgt = &c.bas;
            while (i < f.count) {
                const Line *t = &f.lines[i];
                const char *p;
                size_t n;
                if (match_eq_kw_only(t, "=POD")) { podon = 1; i++; continue; }
                if (match_eq_kw_only(t, "=CUT")) { podon = 0; i++; continue; }
                if (match_star_slash(t)) { i++; break; }
                p = t->s; n = t->n;
                n = chop_slash_comment(p, n);
                n = chop_backslash_space(p, n);
                if (!podon) {
                    buf_add(tgt, p, n);
                    buf_ch(tgt, '\n');
                }
                i++;
            }
            continue;
        }

        if (match_eq_section(l, "=section", &cap, &clen) ||
            match_eq_section(l, ".function", &cap, &clen)) {
            set_name(&c.basic_function_name, cap, clen);
            i++;
            continue;
        }

        if (match_bes(l, "besFUNCTION", &cap, &clen)) {
            set_name(&c.module_function_name, cap, clen);
            open_bas_file(&c, path);
            push_decl(&c.subs, &c.nsubs, &c.capsubs,
                      c.basic_function_name, c.module_function_name);
            i++;
            continue;
        }

        if (match_bes(l, "besCOMMAND", &cap, &clen)) {
            set_name(&c.module_function_name, cap, clen);
            open_bas_file(&c, path);
            push_decl(&c.coms, &c.ncoms, &c.capcoms,
                      c.basic_function_name, c.module_function_name);
            i++;
            continue;
        }

        if (match_comment_function(l)) {
            open_header_file(&c, path);
            buf_str(&c.header, "/*FUNDEF*/\n");
            i++;
            while (i < f.count) {
                const Line *t = &f.lines[i];
                if (match_paren_brace(t)) { i++; break; }
                buf_ch(&c.header, '\n');
                buf_add(&c.header, t->s, t->n);
                i++;
            }
            buf_str(&c.header, ");\n/*FEDNUF*/\n");
            continue;
        }

        i++;
    }

    buf_str(&c.header, "#ifdef __cplusplus\n}\n#endif\n#endif\n");

    /* the collected BASIC declarations */
    if (c.bas_opened) {
        size_t max_bas = 0, max_c = 0;

        for (size_t k = 0; k < c.nsubs; k++) {
            size_t bl = strlen(c.subs[k].basic), cl = strlen(c.subs[k].cname);
            if (bl > max_bas) max_bas = bl;
            if (cl > max_c) max_c = cl;
        }
        for (size_t k = 0; k < c.ncoms; k++) {
            size_t bl = strlen(c.coms[k].basic), cl = strlen(c.coms[k].cname);
            if (bl > max_bas) max_bas = bl;
            if (cl > max_c) max_c = cl;
        }

        if (c.nsubs > 0)
            buf_str(&c.bas, "' FUNCTION DECLARATIONS \n");
        for (size_t k = 0; k < c.nsubs; k++) {
            size_t bl = strlen(c.subs[k].basic), cl = strlen(c.subs[k].cname);
            buf_str(&c.bas, "declare sub     ::");
            buf_str(&c.bas, c.subs[k].basic);
            for (size_t x = bl; x < max_bas; x++) buf_ch(&c.bas, ' ');
            buf_str(&c.bas, " alias \"");
            buf_str(&c.bas, c.subs[k].cname);
            buf_ch(&c.bas, '"');
            for (size_t x = cl; x < max_c; x++) buf_ch(&c.bas, ' ');
            buf_str(&c.bas, " lib \"");
            buf_str(&c.bas, c.module_name ? c.module_name : "");
            buf_str(&c.bas, "\"\n");
        }

        if (c.ncoms > 0)
            buf_str(&c.bas, "\n' COMMAND DECLARATIONS \n");
        for (size_t k = 0; k < c.ncoms; k++) {
            size_t bl = strlen(c.coms[k].basic), cl = strlen(c.coms[k].cname);
            buf_str(&c.bas, "declare command ::");
            buf_str(&c.bas, c.coms[k].basic);
            for (size_t x = bl; x < max_bas; x++) buf_ch(&c.bas, ' ');
            buf_str(&c.bas, " alias \"");
            buf_str(&c.bas, c.coms[k].cname);
            buf_ch(&c.bas, '"');
            for (size_t x = cl; x < max_c; x++) buf_ch(&c.bas, ' ');
            buf_str(&c.bas, " lib \"");
            buf_str(&c.bas, c.module_name ? c.module_name : "");
            buf_str(&c.bas, "\"\n");
        }

        buf_str(&c.bas, "\nend module\n");
    }

    if (c.header_opened && c.open_header_name)
        write_if_different(c.open_header_name, &c.header, "header");
    if (c.bas_opened && c.open_bas_name)
        write_if_different(c.open_bas_name, &c.bas, "bas");
}

int main(int argc, char **argv)
{
    if (argc < 2) return 0;
    for (int i = 1; i < argc; i++)
        process_file(argv[i]);
    return 0;
}
