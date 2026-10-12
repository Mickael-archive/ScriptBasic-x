/*
 * t2h.c -- C23 replacement for t2h.pl, the texi to html converter
 *
 * The Perl original is the tool that turns the texinfo documentation into the
 * html pages shipped with the interpreter (and into the html-help files of the
 * Windows build).  This port emits byte-identical files: the output templates
 * are the heredocs of t2h.pl, extracted into t2h_text.h.
 *
 * Two places where the Perl original iterates a hash are made deterministic
 * here, they are documented in OLD/README.md:
 *
 *   - `keys %CINDEX' in the .hhk writer: the entries are written in the order
 *     they appear in the document,
 *   - the warnings of the original go to stderr, as before.
 *
 * Usage:
 *
 *     t2h input.texi [output-basename]
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t2h_text.h"

/* ------------------------------------------------------------------ memory */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("t2h: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("t2h: out of memory\n", stderr);
        exit(1);
    }
    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static char *xmemdup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* --------------------------------------------------------------- strings */

typedef struct {
    char *p;
    size_t n, cap;
} SB;

static void sb_init(SB *b)
{
    b->cap = 64;
    b->n = 0;
    b->p = xmalloc(b->cap);
    b->p[0] = '\0';
}

static void sb_need(SB *b, size_t extra)
{
    if (b->n + extra + 1 > b->cap) {
        while (b->n + extra + 1 > b->cap) b->cap *= 2;
        b->p = xrealloc(b->p, b->cap);
    }
}

static void sb_puts(SB *b, const char *s)
{
    size_t l = strlen(s);
    sb_need(b, l);
    memcpy(b->p + b->n, s, l);
    b->n += l;
    b->p[b->n] = '\0';
}

static void sb_putn(SB *b, const char *s, size_t l)
{
    sb_need(b, l);
    memcpy(b->p + b->n, s, l);
    b->n += l;
    b->p[b->n] = '\0';
}

static void sb_putc(SB *b, char c)
{
    sb_need(b, 1);
    b->p[b->n++] = c;
    b->p[b->n] = '\0';
}

static char *sb_detach(SB *b)
{
    char *p = b->p;
    b->p = NULL;
    b->n = b->cap = 0;
    return p;
}

static void sb_free(SB *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

/* append a printf() formatted string */
static void sb_printf_into(SB *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        sb_need(b, (size_t)n);
        vsnprintf(b->p + b->n, (size_t)n + 1, fmt, ap);
        b->n += (size_t)n;
    }
    va_end(ap);
}

/* the string a printf() call wants, on the heap */
static char *sb_vsprintf(const char *fmt, va_list ap);

static char *sb_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *r = sb_vsprintf(fmt, ap);
    va_end(ap);
    return r;
}

static char *sb_vsprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) n = 0;
    char *buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

/* ------------------------------------------------------------ string list */

typedef struct {
    char **v;
    int n, cap;
} Vec;

static Vec *vec_new(void)
{
    Vec *l = xmalloc(sizeof *l);
    l->v = NULL;
    l->n = l->cap = 0;
    return l;
}

static void vec_push(Vec *l, char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = s;
}

/* ------------------------------------------------------------- string map */

typedef struct {
    char *key;
    char *val;
} KV;

typedef struct {
    KV *v;
    int n, cap;
} Map;

static Map *map_new(void)
{
    Map *m = xmalloc(sizeof *m);
    m->v = NULL;
    m->n = m->cap = 0;
    return m;
}

static KV *map_find(Map *m, const char *key)
{
    for (int i = 0; i < m->n; i++)
        if (strcmp(m->v[i].key, key) == 0) return &m->v[i];
    return NULL;
}

static const char *map_get(Map *m, const char *key)
{
    KV *e = map_find(m, key);
    return e ? e->val : NULL;
}

static KV *map_slot(Map *m, const char *key)
{
    KV *e = map_find(m, key);
    if (e) return e;
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->v = xrealloc(m->v, (size_t)m->cap * sizeof *m->v);
    }
    e = &m->v[m->n++];
    e->key = xstrdup(key);
    e->val = xstrdup("");
    return e;
}

static void map_set(Map *m, const char *key, const char *val)
{
    KV *e = map_slot(m, key);
    free(e->val);
    e->val = xstrdup(val ? val : "");
}

static void map_concat(Map *m, const char *key, const char *text)
{
    KV *e = map_slot(m, key);
    size_t a = strlen(e->val), b = strlen(text ? text : "");
    e->val = xrealloc(e->val, a + b + 1);
    memcpy(e->val + a, text ? text : "", b + 1);
}

/* ------------------------------------------------------------ file reading */

static char *read_whole_file(const char *file, int *ok)
{
    FILE *f = fopen(file, "rb");
    if (!f) { *ok = 0; return NULL; }
    SB b;
    sb_init(&b);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_putn(&b, buf, n);
    if (ferror(f)) { fclose(f); *ok = 0; sb_free(&b); return NULL; }
    fclose(f);
    *ok = 1;
    return sb_detach(&b);
}

/* ---------------------------------------------------------------- helpers */

/* Perl's \s (the port always runs on a byte string) */
static int is_space_perl(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_word(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static char *lower_ascii(const char *s)
{
    char *r = xstrdup(s);
    for (char *p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    return r;
}

/* replace every occurrence of `pat` in `s`, returning a new string */
static char *replace_all(const char *s, const char *pat, const char *rep)
{
    SB b;
    sb_init(&b);
    size_t pl = strlen(pat);
    const char *p = s;
    while (*p) {
        if (pl && strncmp(p, pat, pl) == 0) {
            sb_puts(&b, rep);
            p += pl;
        } else {
            sb_putc(&b, *p++);
        }
    }
    return sb_detach(&b);
}

/* ------------------------------------------------------- braced directives */

/* find `@name{(.*?)}' with `.` not matching a newline: Perl's /s is not used,
 * so a newline in front of the closing brace means there is no match */
static int find_braced(const char *s, const char *name, size_t ofs,
                       size_t *start, size_t *end, char **capture)
{
    if (*name == '@') name++;
    size_t nl = strlen(name);
    for (size_t i = ofs; s[i]; i++) {
        if (s[i] != '@') continue;
        if (strncmp(s + i + 1, name, nl) != 0) continue;
        if (s[i + 1 + nl] != '{') continue;
        size_t j = i + 2 + nl;
        while (s[j] && s[j] != '}' && s[j] != '\n') j++;
        if (s[j] != '}') continue;
        *start = i;
        *end = j + 1;
        if (capture) *capture = xmemdup(s + i + 2 + nl, j - (i + 2 + nl));
        return 1;
    }
    return 0;
}

/* the two capture form `@name{(.*?),(.*?)}' used by @uref */
static int find_braced2(const char *s, const char *name, size_t ofs,
                        size_t *start, size_t *end, char **c1, char **c2)
{
    if (*name == '@') name++;
    size_t nl = strlen(name);
    for (size_t i = ofs; s[i]; i++) {
        if (s[i] != '@') continue;
        if (strncmp(s + i + 1, name, nl) != 0) continue;
        if (s[i + 1 + nl] != '{') continue;
        size_t j = i + 2 + nl;
        while (s[j] && s[j] != '}' && s[j] != ',' && s[j] != '\n') j++;
        if (s[j] != ',') continue;
        size_t k = j + 1;
        while (s[k] && s[k] != '}' && s[k] != '\n') k++;
        if (s[k] != '}') continue;
        *start = i;
        *end = k + 1;
        *c1 = xmemdup(s + i + 2 + nl, j - (i + 2 + nl));
        *c2 = xmemdup(s + j + 1, k - (j + 1));
        return 1;
    }
    return 0;
}

/* replace the first `@name{...}' by a prefix, the capture and a suffix */
static char *replace_braced_all(const char *s, const char *name,
                                const char *prefix, const char *suffix)
{
    /*
     * Perl's /g continues the scan *behind* the text it has just inserted, so
     * a directive that the captured text contains is not replaced again.
     * Rescanning the whole string after every replacement (which is what
     * looping over replace_braced_once() from the start would do) does replace
     * it, and the manuals do contain nested directives: the texi of the sample
     * programs has `@code{... @code{PRINT}' on one line.
     */
    SB out;
    sb_init(&out);
    const char *p = s;
    for (;;) {
        size_t a, b;
        char *cap = NULL;
        if (!find_braced(p, name, 0, &a, &b, &cap)) break;
        sb_putn(&out, p, a);
        sb_puts(&out, prefix);
        sb_puts(&out, cap);
        sb_puts(&out, suffix);
        free(cap);
        p += b;
    }
    sb_puts(&out, p);
    return sb_detach(&out);
}

/* `@name{...}' where the capture is used twice: prefix + cap + middle + cap + suffix */
static char *replace_braced_double(const char *s, const char *name,
                                   const char *prefix, const char *middle, const char *suffix)
{
    char *cur = xstrdup(s);
    for (;;) {
        size_t a, b;
        char *cap = NULL;
        if (!find_braced(cur, name, 0, &a, &b, &cap)) break;
        SB out;
        sb_init(&out);
        sb_putn(&out, cur, a);
        sb_puts(&out, prefix);
        sb_puts(&out, cap);
        sb_puts(&out, middle);
        sb_puts(&out, cap);
        sb_puts(&out, suffix);
        sb_puts(&out, cur + b);
        free(cap);
        free(cur);
        cur = sb_detach(&out);
    }
    return cur;
}

/* s[\@end\s+word][rep]g */
static char *replace_end_word(const char *s, const char *word, const char *rep)
{
    SB out;
    sb_init(&out);
    size_t wl = strlen(word);
    const char *p = s;
    while (*p) {
        if (strncmp(p, "@end", 4) == 0 && is_space_perl(p[4])) {
            const char *q = p + 4;
            while (is_space_perl(*q)) q++;
            if (strncmp(q, word, wl) == 0) {
                sb_puts(&out, rep);
                p = q + wl;
                continue;
            }
        }
        sb_putc(&out, *p++);
    }
    return sb_detach(&out);
}

/* s[\@uref\{(.*?),(.*?)\}][<a href="$1">$2</A>]g then
 * s[\@uref\{(.*?)\}][<a href="$1">$1</A>]g */
static char *replace_uref(const char *s)
{
    char *cur = xstrdup(s);
    for (;;) {
        size_t a, b;
        char *c1 = NULL, *c2 = NULL;
        if (!find_braced2(cur, "@uref", 0, &a, &b, &c1, &c2)) break;
        SB out;
        sb_init(&out);
        sb_putn(&out, cur, a);
        sb_puts(&out, "<a href=\"");
        sb_puts(&out, c1);
        sb_puts(&out, "\">");
        sb_puts(&out, c2);
        sb_puts(&out, "</A>");
        sb_puts(&out, cur + b);
        free(c1);
        free(c2);
        free(cur);
        cur = sb_detach(&out);
    }
    for (;;) {
        /* the capture has to be repeated as the link text */
        size_t a, b;
        if (!find_braced(cur, "@uref", 0, &a, &b, NULL)) break;
        char *cap = NULL;
        find_braced(cur, "@uref", 0, &a, &b, &cap);
        SB out;
        sb_init(&out);
        sb_putn(&out, cur, a);
        sb_puts(&out, "<a href=\"");
        sb_puts(&out, cap);
        sb_puts(&out, "\">");
        sb_puts(&out, cap);
        sb_puts(&out, "</A>");
        sb_puts(&out, cur + b);
        free(cap);
        free(cur);
        cur = sb_detach(&out);
    }
    return cur;
}

/* ------------------------------------------------------ titles of sections */

static Map *TOC;                        /* id -> title */
static Map *ITOC;                       /* title -> id */
static Map *BODY;                       /* id -> body text */
static Map *CINDEX;                     /* index entry -> id|id|... */
static Vec *TOCI;                       /* ids in document order */
static char *TITLE, *AUTHOR;

static void strip_nonalnum_all(char *s)         /* s/\W//g */
{
    char *o = s;
    for (char *p = s; *p; p++) if (is_word(*p)) *o++ = *p;
    *o = '\0';
}

static void strip_first_nonalnum(char *s)       /* s/\W// */
{
    for (char *p = s; *p; p++) {
        if (!is_word(*p)) {
            char *q = p;
            do { *q = q[1]; } while (*++q);
            break;
        }
    }
}

static void strip_from_first_space(char *s)     /* s/\s.*$// */
{
    for (char *p = s; *p; p++) {
        if (is_space_perl(*p)) { *p = '\0'; break; }
    }
}

static void strip_from_first_nonalnum(char *s)  /* s/\W.*$// */
{
    for (char *p = s; *p; p++) {
        if (!is_word(*p)) { *p = '\0'; break; }
    }
}

/* t2h.pl's getitoc(): find the section id an @xref refers to */
static const char *getitoc(const char *p_in)
{
    const char *v = map_get(ITOC, p_in);
    if (v) return v;

    const char *r = p_in;
    char *p = lower_ascii(p_in);
    strip_nonalnum_all(p);
    v = map_get(ITOC, p);
    if (v) { free(p); return v; }

    for (int i = 0; i < TOCI->n; i++) {
        const char *title = map_get(TOC, TOCI->v[i]);
        char *k = lower_ascii(title ? title : "");
        strip_first_nonalnum(k);
        int hit = strcmp(k, p) == 0;
        free(k);
        if (hit) { free(p); return TOCI->v[i]; }
    }
    free(p);

    p = lower_ascii(r);
    strip_from_first_space(p);
    strip_from_first_nonalnum(p);
    for (int i = 0; i < TOCI->n; i++) {
        const char *title = map_get(TOC, TOCI->v[i]);
        char *k = lower_ascii(title ? title : "");
        strip_from_first_space(k);
        strip_from_first_nonalnum(k);
        int hit = strcmp(k, p) == 0;
        free(k);
        if (hit) { free(p); return TOCI->v[i]; }
    }
    free(p);
    return NULL;
}

/* ------------------------------------------------------------- htmlizee() */

static char *infile;            /* the global the original interpolates */

static char *htmlizee(const char *in, const char *filext)
{
    if (!filext) filext = "html";       /* $filext = 'html' unless defined $filext; */
    char *line = xstrdup(in);
    char *t;

    t = replace_all(line, "@@", "SAVEALLDOUBLESOBAKA"); free(line); line = t;
    t = replace_all(line, "&", "&amp;");                free(line); line = t;
    t = replace_all(line, "<", "&lt;");                 free(line); line = t;
    t = replace_all(line, ">", "&gt;");                 free(line); line = t;

    t = replace_braced_all(line, "@file",   "`<font size=\"3\"><tt>", "</tt></font>'"); free(line); line = t;
    t = replace_braced_all(line, "@code",   "<font size=\"3\"><tt>",  "</tt></font>");  free(line); line = t;
    t = replace_braced_all(line, "@var",    "<font size=\"3\"><tt>",  "</tt></font>");  free(line); line = t;
    t = replace_braced_all(line, "@command","<font size=\"3\"><tt>",  "</tt></font>");  free(line); line = t;
    t = replace_braced_all(line, "@acronym","<font size=\"3\"><tt>",  "</tt></font>");  free(line); line = t;
    t = replace_braced_all(line, "@b",      "<B>", "</B>");                             free(line); line = t;
    t = replace_braced_all(line, "@strong", "<I>", "</I>");                             free(line); line = t;
    t = replace_braced_all(line, "@emph",   "<I>", "</I>");                             free(line); line = t;
    t = replace_braced_all(line, "@option", "`<font size=\"3\"><tt>", "</tt></font>'"); free(line); line = t;

    t = replace_all(line, "@itemize", "<UL>");          free(line); line = t;
    t = replace_end_word(line, "itemize", "</UL>");     free(line); line = t;
    t = replace_all(line, "@item", "<LI>");             free(line); line = t;
    t = replace_all(line, "@example", "<FONT SIZE=\"3\" COLOR=\"BLUE\"><PRE>"); free(line); line = t;
    t = replace_end_word(line, "example", "</PRE></FONT>");                     free(line); line = t;

    t = replace_all(line, "@{", "{"); free(line); line = t;
    t = replace_all(line, "@}", "}"); free(line); line = t;
    t = replace_all(line, "@$", "$"); free(line); line = t;

    {   /* @xref{...}: resolved against the table of contents */
        for (;;) {
            size_t a, b;
            char *cap = NULL;
            if (!find_braced(line, "@xref", 0, &a, &b, &cap)) break;
            const char *itoc = getitoc(cap);
            SB out;
            sb_init(&out);
            sb_putn(&out, line, a);
            if (filext && filext[0] == '#')
                sb_printf_into(&out, "<a href=\"#%s\">%s</A>", itoc ? itoc : "", cap);
            else
                sb_printf_into(&out, "<a href=\"%s_%s%s\">%s</A>",
                               infile, itoc ? itoc : "", filext ? filext : "", cap);
            sb_puts(&out, line + b);
            free(cap);
            free(line);
            line = sb_detach(&out);
        }
    }

    t = replace_uref(line);                                 free(line); line = t;
    t = replace_braced_double(line, "@email", "<a href=\"mailto:", "\">", "</A>"); free(line); line = t;

    t = replace_all(line, "SAVEALLDOUBLESOBAKA", "@"); free(line); line = t;
    t = replace_all(line, "\n\n", "\n<P>\n");          free(line); line = t;
    return line;
}

/* ------------------------------------------------------------- the scanner */

static char *inpath = NULL;
static int chapter, section, subsection, subsubsection;
static char *bodindex = NULL;

/* the lines of the input are kept with their newline, like @lines = <F> */
static Vec *read_lines(const char *content)
{
    Vec *l = vec_new();
    const char *p = content;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) + 1 : strlen(p);
        vec_push(l, xmemdup(p, n));
        p += n;
    }
    return l;
}

/* the capture of `(.*)$' with `.` not matching a newline */
static char *capture_rest(const char *p)
{
    const char *e = p;
    while (*e && *e != '\n') e++;
    return xmemdup(p, (size_t)(e - p));
}

/* /^\@word\s+(.*)$/ */
static int match_directive(const char *line, const char *word, char **capture)
{
    if (line[0] != '@') return 0;
    size_t l = strlen(word);
    if (strncmp(line + 1, word, l) != 0) return 0;
    const char *p = line + 1 + l;
    if (!is_space_perl(*p)) return 0;
    while (is_space_perl(*p) && *p != '\n') p++;
    if (capture) *capture = capture_rest(p);
    return 1;
}

/* /^\@[cfvkt]index\s+(.*)$/ */
static int match_index_directive(const char *line, char **capture)
{
    if (line[0] != '@') return 0;
    char c = line[1];
    if (c != 'c' && c != 'f' && c != 'v' && c != 'k' && c != 't') return 0;
    if (strncmp(line + 2, "index", 5) != 0) return 0;
    const char *p = line + 7;
    if (!is_space_perl(*p)) return 0;
    while (is_space_perl(*p) && *p != '\n') p++;
    if (capture) *capture = capture_rest(p);
    return 1;
}

/* /^\s*\@word\s+/ */
static int match_word_arg(const char *line, const char *word)
{
    const char *p = line;
    while (is_space_perl(*p)) p++;
    if (*p != '@') return 0;
    p++;
    size_t l = strlen(word);
    if (strncmp(p, word, l) != 0) return 0;
    return is_space_perl(p[l]) != 0;
}

/* /^\s*\@word$/ with the newline of the line at the end of the string */
static int match_word_alone(const char *line, const char *word)
{
    const char *p = line;
    while (is_space_perl(*p) && *p != '\n') p++;
    if (*p != '@') return 0;
    p++;
    size_t l = strlen(word);
    if (strncmp(p, word, l) != 0) return 0;
    p += l;
    return *p == '\0' || *p == '\n';
}

/* /^\s*\@end\s+word\s*$/ */
static int match_end_directive(const char *line, const char *word)
{
    const char *p = line;
    while (is_space_perl(*p)) p++;
    if (strncmp(p, "@end", 4) != 0) return 0;
    p += 4;
    if (!is_space_perl(*p)) return 0;
    while (is_space_perl(*p)) p++;
    size_t l = strlen(word);
    if (strncmp(p, word, l) != 0) return 0;
    p += l;
    while (is_space_perl(*p)) p++;
    return *p == '\0';
}

/* /^\s*\@menu\s*$/ */
static int match_menu_line(const char *line)
{
    const char *p = line;
    while (is_space_perl(*p)) p++;
    if (strncmp(p, "@menu", 5) != 0) return 0;
    p += 5;
    while (is_space_perl(*p)) p++;
    return *p == '\0';
}

static void scan(const Vec *lines)
{
    for (int i = 0; i < lines->n; i++) {
        const char *line = lines->v[i];
        char *cap = NULL;

        if (match_directive(line, "title", &cap)) {
            free(TITLE);
            TITLE = htmlizee(cap, NULL);
            free(cap);
            continue;
        }
        if (match_directive(line, "author", &cap)) {
            free(AUTHOR);
            AUTHOR = htmlizee(cap, NULL);
            free(cap);
            continue;
        }
        if (match_directive(line, "chapter", &cap)) {
            chapter++;
            section = subsection = subsubsection = 0;
            free(bodindex);
            bodindex = sb_printf("%d.", chapter);
            map_set(TOC, bodindex, cap);
            map_set(ITOC, cap, bodindex);
            vec_push(TOCI, xstrdup(bodindex));
            free(cap);
            continue;
        }
        if (match_directive(line, "section", &cap)) {
            section++;
            subsection = subsubsection = 0;
            free(bodindex);
            bodindex = sb_printf("%d.%d.", chapter, section);
            map_set(TOC, bodindex, cap);
            map_set(ITOC, cap, bodindex);
            vec_push(TOCI, xstrdup(bodindex));
            free(cap);
            continue;
        }
        if (match_directive(line, "subsection", &cap)) {
            subsection++;
            subsubsection = 0;
            free(bodindex);
            bodindex = sb_printf("%d.%d.%d.", chapter, section, subsection);
            map_set(TOC, bodindex, cap);
            map_set(ITOC, cap, bodindex);
            vec_push(TOCI, xstrdup(bodindex));
            free(cap);
            continue;
        }
        if (match_directive(line, "subsubsection", &cap)) {
            subsubsection++;
            free(bodindex);
            bodindex = sb_printf("%d.%d.%d.%d.", chapter, section, subsection, subsubsection);
            map_set(TOC, bodindex, cap);
            map_set(ITOC, cap, bodindex);
            vec_push(TOCI, xstrdup(bodindex));
            free(cap);
            continue;
        }
        if (match_index_directive(line, &cap)) {
            const char *sofar = map_get(CINDEX, cap);
            if (sofar) {
                char *joined = sb_printf("%s|%s", sofar, bodindex ? bodindex : "");
                map_set(CINDEX, cap, joined);
                free(joined);
            } else {
                map_set(CINDEX, cap, bodindex ? bodindex : "");
            }
            free(cap);
            continue;
        }
        map_concat(BODY, bodindex ? bodindex : "", line);
    }
}

/* the body of a section with the texinfo bookkeeping lines removed */
static char *body_text(const char *body)
{
    Vec *lns = vec_new();
    const char *p = body ? body : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        vec_push(lns, xmemdup(p, n));
        p += n + (nl ? 1 : 0);
    }
    /* Perl's `split /\n/` throws the empty fields at the end of the list away,
     * so a body that ends on a blank line does not keep a trailing newline. */
    while (lns->n > 0 && lns->v[lns->n - 1][0] == '\0')
        free(lns->v[--lns->n]);
    Vec *plns = vec_new();
    for (int i = 0; i < lns->n; i++) {
        const char *line = lns->v[i];
        if (match_word_arg(line, "bye")) break;
        if (match_word_alone(line, "bye")) break;
        if (match_word_arg(line, "c")) continue;
        if (match_word_arg(line, "node")) continue;
        if (match_word_alone(line, "c")) continue;
        if (match_word_arg(line, "cindex")) continue;
        if (match_word_alone(line, "cindex")) continue;
        if (match_word_arg(line, "opindex")) continue;
        if (match_word_alone(line, "opindex")) continue;
        if (match_word_arg(line, "vindex")) continue;
        if (match_word_alone(line, "vindex")) continue;
        if (match_menu_line(line)) {
            while (i < lns->n) {
                const char *l = lns->v[i++];
                if (match_end_directive(l, "menu")) break;
            }
            continue;
        }
        vec_push(plns, xstrdup(line));
    }
    SB b;
    sb_init(&b);
    for (int i = 0; i < plns->n; i++) {
        if (i) sb_putc(&b, '\n');
        sb_puts(&b, plns->v[i]);
    }
    return sb_detach(&b);
}

/* ----------------------------------------------------------------- helpers */

static void emit(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
}

static void emit_repeat(FILE *f, const char *s, int times)
{
    for (int i = 0; i < times; i++) fputs(s, f);
}

static int nondigits(const char *s)
{
    int n = 0;
    for (const char *p = s; *p; p++) if (!isdigit((unsigned char)*p)) n++;
    return n;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

/* the k-dot prefix of a section number: /^(\d+\.\d+\. ...)/ */
static char *section_prefix(const char *toc, int dots)
{
    int seen = 0;
    const char *p = toc;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return NULL;      /* needs \d+ */
        while (isdigit((unsigned char)*p)) p++;
        if (*p != '.') return NULL;
        p++;
        if (++seen == dots) return xmemdup(toc, (size_t)(p - toc));
    }
    return NULL;
}

static FILE *open_out(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Can not open output file %s\n", path);
        exit(errno ? errno : 255);
    }
    return f;
}

int main(int argc, char **argv)
{
    TOC = map_new();
    ITOC = map_new();
    BODY = map_new();
    CINDEX = map_new();
    TOCI = vec_new();
    TITLE = xstrdup("");
    AUTHOR = xstrdup("");

    char *in = xstrdup(argc >= 2 ? argv[1] : "");
    for (char *p = in; *p; p++) if (*p == '\\') *p = '/';

    int ok = 0;
    char *content = read_whole_file(in, &ok);
    if (!ok) {
        fprintf(stderr, "Can not open input file %s\n", in);
        return errno ? errno : 255;
    }
    Vec *lines = read_lines(content);
    free(content);

    infile = xstrdup(in);
    scan(lines);
    free(in);

    const char *outname = argc >= 3 ? argv[2] : NULL;
    if (outname) {
        free(infile);
        infile = xstrdup(outname);
    } else {
        char *dot = strchr(infile, '.');
        if (dot) *dot = '\0';
    }
    char *slash = strrchr(infile, '/');
    if (slash) {
        inpath = xmemdup(infile, (size_t)(slash - infile) + 1);
        char *base = xstrdup(slash + 1);
        free(infile);
        infile = base;
    } else {
        inpath = xstrdup("./");
    }

    char *fullpath = sb_printf("%s%s.html", inpath, infile);
    FILE *FULL = fopen(fullpath, "w");
    if (!FULL) {
        fprintf(stderr, "Cannot open %s\n", fullpath);
        return errno ? errno : 255;
    }
    emit(FULL, T01, TITLE, TITLE, TITLE, BODYSTART, TITLE, AUTHOR);

    char *path = sb_printf("%s%s_toc.html", inpath, infile);
    FILE *F = open_out(path);
    emit(F, T02, TITLE, TITLE, TITLE, BODYSTART, TITLE, AUTHOR);

    int plevel = 0;
    for (int i = 0; i < TOCI->n; i++) {
        const char *toc = TOCI->v[i];
        if (ends_with(toc, ".1.")) {
            emit(F, "<UL>\n");
            emit(FULL, "<UL>\n");
        }
        int level = nondigits(toc);
        if (level < plevel) {
            emit_repeat(F, "</UL>\n", plevel - level);
            emit_repeat(FULL, "</UL>\n", plevel - level);
        }
        plevel = level;
        char *tochtml = htmlizee(map_get(TOC, toc) ? map_get(TOC, toc) : "", NULL);
        emit(F, T03, infile, toc, toc, tochtml);
        emit(FULL, T04, toc, toc, tochtml);
        free(tochtml);
    }
    emit_repeat(FULL, "</UL>\n", plevel);
    emit_repeat(F, "</UL>\n", plevel);
    emit(F, T05, BODYEND);
    fclose(F);

    path = sb_printf("%s%s_title.htm", inpath, infile);
    F = open_out(path);
    emit(F, T06, TITLE, TITLE, TITLE, HBODYSTART, TITLE, AUTHOR, BODYEND);
    fclose(F);

    path = sb_printf("%s%s.hhc", inpath, infile);
    F = open_out(path);
    emit(F, T07);
    plevel = 0;
    for (int i = 0; i < TOCI->n; i++) {
        const char *toc = TOCI->v[i];
        if (ends_with(toc, ".1.")) emit(F, "<UL>\n");
        int level = nondigits(toc);
        if (level < plevel) emit_repeat(F, "</UL>\n", plevel - level);
        plevel = level;
        char *tochtml = htmlizee(map_get(TOC, toc) ? map_get(TOC, toc) : "", NULL);
        emit(F, T08, tochtml, infile, toc);
        free(tochtml);
    }
    emit(F, T09, BODYEND);
    fclose(F);

    path = sb_printf("%s%s.hhk", inpath, infile);
    F = open_out(path);
    emit(F, T10);
    for (int i = 0; i < TOCI->n; i++) {
        const char *toc = TOCI->v[i];
        char *tochtml = htmlizee(map_get(TOC, toc) ? map_get(TOC, toc) : "", NULL);
        emit(F, T11, tochtml, infile, toc);
        free(tochtml);
    }
    for (int i = 0; i < CINDEX->n; i++) {
        char *tochtml = htmlizee(CINDEX->v[i].key, NULL);
        const char *ids = CINDEX->v[i].val;
        const char *p = ids;
        while (*p) {
            const char *bar = strchr(p, '|');
            size_t n = bar ? (size_t)(bar - p) : strlen(p);
            char *id = xmemdup(p, n);
            emit(F, T12, tochtml, infile, id);
            free(id);
            if (!bar) break;
            p = bar + 1;
        }
        free(tochtml);
    }
    emit(F, T13, BODYEND);
    fclose(F);

    path = sb_printf("%s%s.hhp", inpath, infile);
    F = open_out(path);
    emit(F, T14, infile, infile, infile, infile, TITLE, infile);
    for (int i = 0; i < TOCI->n; i++) emit(F, T15, infile, TOCI->v[i]);
    emit(F, T16);
    fclose(F);

    for (int i = 0; i < TOCI->n; i++) {
        const char *toc = TOCI->v[i];
        char *tochtml = htmlizee(map_get(TOC, toc) ? map_get(TOC, toc) : "", NULL);

        path = sb_printf("%s%s_%shtml", inpath, infile, toc);
        F = open_out(path);
        emit(F, T17, TITLE, toc, tochtml, TITLE, tochtml, TITLE, tochtml, BODYSTART);

        int ulc = 1;
        emit(FULL, "<P><a href=\"#contents\">[Contents]</A><BR>\n");
        emit(F, "<UL><a href=\"%s_toc.html\">[Contents]</A><BR>\n", infile);
        for (int dots = 1; dots <= 4; dots++) {
            char *sec = section_prefix(toc, dots);
            if (!sec) continue;
            if (strcmp(toc, sec) != 0) {
                ulc++;
                char *tochtml2 = htmlizee(map_get(TOC, sec) ? map_get(TOC, sec) : "", NULL);
                emit(F, "<UL><a href=\"%s_%shtml\">%s %s</A><BR>\n", infile, sec, sec, tochtml2);
                free(tochtml2);
            }
            free(sec);
        }
        if (ulc > 0) emit_repeat(F, "</UL>\n", ulc);

        emit(F, "<P>\n");
        emit(FULL, "<P>\n");

        int n = nondigits(toc);         /* $n = $dotcounter =~ s/\.//g */
        emit(F, "<H1>%s %s</H1>\n", toc, tochtml);
        emit(FULL, "<A name=\"%s\"><H%d>%s %s</H%d></A>\n", toc, n, toc, tochtml, n);

        SB step;
        sb_init(&step);
        if (i > 0) sb_printf_into(&step, "<A HREF=\"%s_%shtml\">[&lt;&lt;&lt;]</A> ", infile, TOCI->v[i - 1]);
        else sb_puts(&step, "[&lt;&lt;&lt;] ");
        if (i < TOCI->n - 1) sb_printf_into(&step, "<A HREF=\"%s_%shtml\">[&gt;&gt;&gt;]</A>", infile, TOCI->v[i + 1]);
        else sb_puts(&step, "[&gt;&gt;&gt;]");
        emit(F, "%s\n", step.p);

        int u = 1;
        int lvl = nondigits(toc);
        int toclen = nondigits(toc);
        for (int j = i + 1; j < TOCI->n; j++) {
            const char *sec = TOCI->v[j];
            int seclen = nondigits(sec);
            if (toclen >= seclen) break;
            int level = nondigits(sec);
            if (level > lvl) {
                emit_repeat(F, "<UL>\n", level - lvl);
                u += lvl - level;
                if (u < 0) u = 0;
                if (level > lvl + 1)
                    printf("Warning: daingling section \"%s %s\"\n", sec,
                           map_get(TOC, sec) ? map_get(TOC, sec) : "");
            }
            if (level < lvl) {
                emit_repeat(F, "</UL>\n", lvl - level);
                u -= lvl - level;
                if (u < 0) u = 0;
            }
            lvl = level;
            char *sectml = htmlizee(map_get(TOC, sec) ? map_get(TOC, sec) : "", NULL);
            emit(F, T18, infile, sec, sec, sectml);
            free(sectml);
        }
        if (u > 0) emit_repeat(F, "</UL>\n", u);
        emit(F, "<HR>\n");
        emit(F, "<P>\n");

        char *plns = body_text(map_get(BODY, toc));
        char *html = htmlizee(plns, NULL);
        fputs(html, F);
        char *htmlf = htmlizee(plns, "#");
        fputs(htmlf, FULL);
        free(html);
        free(htmlf);
        free(plns);
        emit(F, T19, step.p, BODYEND);
        fclose(F);
        sb_free(&step);

        path = sb_printf("%s%s_%shtm", inpath, infile, toc);
        F = open_out(path);
        emit(F, T20, TITLE, tochtml, TITLE, tochtml, TITLE, tochtml, HBODYSTART);
        emit(F, "<H1>%s</H1>\n", tochtml);

        SB step2;
        sb_init(&step2);
        if (i > 0) sb_printf_into(&step2, "<A HREF=\"%s_%shtm\">[&lt;&lt;&lt;]</A> ", infile, TOCI->v[i - 1]);
        else sb_puts(&step2, "[&lt;&lt;&lt;] ");
        if (i < TOCI->n - 1) sb_printf_into(&step2, "<A HREF=\"%s_%shtm\">[&gt;&gt;&gt;]</A>", infile, TOCI->v[i + 1]);
        else sb_puts(&step2, "[&gt;&gt;&gt;]");
        emit(F, "%s\n", step2.p);
        emit(F, "<P>\n");

        char *plns2 = body_text(map_get(BODY, toc));
        char *html2 = htmlizee(plns2, "htm");
        fputs(html2, F);
        free(html2);
        free(plns2);
        emit(F, T21, step2.p, BODYEND);
        fclose(F);
        sb_free(&step2);

        free(tochtml);
    }
    fclose(FULL);
    return 0;
}
