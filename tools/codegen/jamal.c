/*
 * jamal.c -- C23 replacement for jamal.pl
 *
 * Jamal is the macro preprocessor used to turn the *.jam / *.jim sources of
 * this project into real makefiles, scripts and documentation.  This program
 * is a faithful port of jamal.pl: for the inputs the build feeds it, it emits
 * byte-identical output.  The Perl extension interface needs a Perl
 * interpreter: the `{use esd}' module the documentation uses is built in here
 * (see the esd section below), other extension modules are reported as
 * unsupported.
 *
 * The original iterates no hash, so its output is deterministic and so is
 * this port.  Perl's odd corners are reproduced on purpose and flagged in
 * comments where they matter (bareword `char` in the whitespace skipper,
 * scalars that are false because they are the string "0", ...).
 *
 * Usage:
 *
 *     jamal [-options] input [output]
 *
 *     -? or -h    help                       -m file   macro definition file
 *     -I dir      include directory          -0        suppress output
 *     -d          put error text into result -t file   trace file
 *     -Dname=val  predefine a macro          -i        install (unsupported)
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define JAMAL_VERSION "3.00"

/* ------------------------------------------------------------------ memory */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("jamal: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("jamal: out of memory\n", stderr);
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

/* NUL-safe copy of a byte range */
static char *xmemdup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ------------------------------------------------------- string buffer (SB) */

typedef struct {
    char *p;
    size_t len, cap;
} SB;

static void sb_init(SB *b)
{
    b->cap = 64;
    b->len = 0;
    b->p = xmalloc(b->cap);
    b->p[0] = '\0';
}

static void sb_need(SB *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->p = xrealloc(b->p, b->cap);
    }
}

static void sb_puts(SB *b, const char *s)
{
    size_t n = strlen(s);
    sb_need(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_putn(SB *b, const char *s, size_t n)
{
    sb_need(b, n);
    if (n) memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_putc(SB *b, char c)
{
    sb_need(b, 1);
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
}


static char *sb_detach(SB *b)
{
    char *p = b->p;
    b->p = NULL;
    b->len = b->cap = 0;
    return p;
}

static void sb_free(SB *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

/* --------------------------------------------------- string vector (Vec) */

typedef struct {
    char **v;
    int n, cap;
} Vec;

static Vec *vec_new(void)
{
    Vec *l = xmalloc(sizeof *l);
    l->n = 0;
    l->cap = 8;
    l->v = xmalloc((size_t)l->cap * sizeof(char *));
    return l;
}

static void vec_push(Vec *l, char *s)
{
    if (l->n == l->cap) {
        l->cap *= 2;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(char *));
    }
    l->v[l->n++] = s;
}

static void vec_free(Vec *l)
{
    if (!l) return;
    for (int i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    free(l);
}

static char *vec_get(const Vec *l, int i)
{
    if (!l || i < 0 || i >= l->n) return NULL;
    return l->v[i];
}

/* ------------------------------------------------ Perl scalar semantics --- */

/* Perl truthiness: undef, "" and "0" are false; everything else is true.
 * (This is why a fragment that happens to be the string "0" is dropped by
 * the argument substituter below -- that is the original behaviour.) */
static int p_true(const char *s)
{
    if (!s) return 0;
    if (s[0] == '\0') return 0;
    if (s[0] == '0' && s[1] == '\0') return 0;
    return 1;
}

/* Perl numeric coercion of a string: leading spaces, optional sign, then a
 * number; anything else is 0. */
static double p_num(const char *s)
{
    if (!s) return 0.0;
    return strtod(s, NULL);
}

/* Perl stringify of a double the way jamal's arithmetic uses it.  Perl prints
 * integral values without a decimal point and uses %.15g otherwise. */
static void p_numstr(char *buf, size_t n, double d)
{
    if (d == (double)(long long)d && d < 1e15 && d > -1e15)
        snprintf(buf, n, "%lld", (long long)d);
    else
        snprintf(buf, n, "%.15g", d);
}

/* Perl quotemeta: backslash before every byte that is not [A-Za-z0-9_]. */
static char *p_quotemeta(const char *s)
{
    SB b;
    sb_init(&b);
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        if (!(isalnum(*q) || *q == '_')) sb_putc(&b, '\\');
        sb_putc(&b, (char)*q);
    }
    return sb_detach(&b);
}

/* count new lines in a string (Perl: tr/\n/\n/) */
static size_t cnl(const char *s)
{
    size_t n = 0;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}


/* ----------------------------------------------------------- global state */

static char *macroOpen, *macroClose;      /* current delimiters ("{" "}") */
static size_t mopenCNL, mcloseCNL;        /* new lines inside the delimiters */

static int OutputError = 0;               /* -d: put error text into result */
static int SuppressOutput = 0;            /* -0: no output */
static const char *TraceFile = NULL;      /* -t: trace file name */
static const char *LastTraceFile = "";
static char *Source = NULL;               /* input file */
static char *Destination = NULL;          /* output file (may be NULL) */
static Vec *Macrofiles = NULL;            /* -m files */
static Vec *IncludeDirs = NULL;           /* -I directories (kept for help) */

static char *BaseModule = NULL;     /* {with pkg} ... */
static Vec *BaseModules = NULL;

static int WeAreInLoop = 0;
static Vec *LoopMacroStack = NULL;        /* only used by {#dir} */

static int SupressWarnings = 0;           /* spelled as in the original */

/* global date/time formatting, settable by {#months}/{#days}/{#format} */
static Vec *MonthNames = NULL;
static Vec *DayNames = NULL;
static char *AM = NULL, *PM = NULL;
static char *TimeFormat = NULL, *DateFormat = NULL;
static time_t timeNow;

/* ------------------------------------------------------------- macro bits */

typedef struct {
    char *name;
    char *value;      /* NULL means "not defined" */
    Vec  *args;       /* formal parameter names, quotemeta'd, or NULL */
} MacroEntry;

static MacroEntry *Macros = NULL;
static int nMacros = 0, capMacros = 0;

static MacroEntry *macro_find(const char *name)
{
    for (int i = 0; i < nMacros; i++)
        if (strcmp(Macros[i].name, name) == 0)
            return &Macros[i];
    return NULL;
}

/* saved values so {#restore name} can bring a macro back (Perl %MacroStack) */
typedef struct {
    char *name;
    char *value;
    Vec  *args;
} SavedMacro;

static SavedMacro *MacroStack = NULL;
static int nStack = 0, capStack = 0;

static void stack_push_saved(const char *name, char *value, Vec *args)
{
    if (nStack == capStack) {
        capStack = capStack ? capStack * 2 : 16;
        MacroStack = xrealloc(MacroStack, (size_t)capStack * sizeof *MacroStack);
    }
    MacroStack[nStack].name = xstrdup(name);
    MacroStack[nStack].value = value;
    MacroStack[nStack].args = args;
    nStack++;
}

/* return the most recently saved value for `name`, removing it */
static int stack_pop_saved(const char *name, char **value, Vec **args)
{
    for (int i = nStack - 1; i >= 0; i--) {
        if (strcmp(MacroStack[i].name, name) == 0) {
            *value = MacroStack[i].value;
            *args = MacroStack[i].args;
            free(MacroStack[i].name);
            for (int j = i; j < nStack - 1; j++) MacroStack[j] = MacroStack[j + 1];
            nStack--;
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------- included file / line */

typedef struct {
    char *file;
    long line;
} FileLine;

static FileLine *FileLines = NULL;
static int nFileLines = 0, capFileLines = 0;

static long *line_of(const char *file, int create)
{
    for (int i = 0; i < nFileLines; i++)
        if (strcmp(FileLines[i].file, file) == 0)
            return &FileLines[i].line;
    if (!create) {
        static long zero = 0;
        return &zero;
    }
    if (nFileLines == capFileLines) {
        capFileLines = capFileLines ? capFileLines * 2 : 16;
        FileLines = xrealloc(FileLines, (size_t)capFileLines * sizeof *FileLines);
    }
    FileLines[nFileLines].file = xstrdup(file);
    FileLines[nFileLines].line = 0;
    nFileLines++;
    return &FileLines[nFileLines - 1].line;
}

/* stack of files currently being processed (Perl @FILE) */
static Vec *FileStack = NULL;

static const char *cur_file(void)
{
    if (FileStack && FileStack->n > 0) return FileStack->v[FileStack->n - 1];
    return "";
}

/* ---------------------------------------------------------- Perl's split */

static int is_space_perl(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Perl split($pat,$str,$limit) for the two pattern shapes Jamal builds:
 * a single literal byte, or /\s+/ (one or more whitespace).
 *   limit < 0  keep trailing empty fields
 *   limit == 0 drop trailing empty fields (Perl's default)
 *   limit > 0  at most `limit` fields, the last one takes the remainder
 * Perl returns an empty list for an empty subject. */
static Vec *split_core(const char *s, int is_ws, char sep, int limit)
{
    Vec *out = vec_new();
    size_t len = strlen(s);
    if (len == 0) return out;

    size_t start = 0, i = 0;
    while (i < len) {
        size_t mlen = 0;
        if (is_ws) {
            if (is_space_perl(s[i])) {
                while (i + mlen < len && is_space_perl(s[i + mlen])) mlen++;
            }
        } else if (s[i] == sep) {
            mlen = 1;
        }
        if (mlen) {
            if (limit > 0 && out->n == limit - 1) break;
            vec_push(out, xmemdup(s + start, i - start));
            i += mlen;
            start = i;
        } else {
            i++;
        }
    }
    vec_push(out, xmemdup(s + start, len - start));
    if (limit == 0) {
        while (out->n > 0 && out->v[out->n - 1][0] == '\0') {
            free(out->v[out->n - 1]);
            out->n--;
        }
    }
    return out;
}

/* Jamal's Split(): the very first character of the string is the separator.
 * A whitespace separator other than newline means "runs of whitespace" and
 * leading whitespace is stripped; any other character is used literally.
 * The optional limit defaults to -1.  Pass INT_MIN for "not given". */
static Vec *Split(const char *string, int limit)
{
    int lim = (limit == INT_MIN) ? -1 : limit;

    if (!string || string[0] == '\0') {
        /* Perl: substr("",0,1) and substr("",1) are both "" and
         * split("","",-1) is the empty list. */
        return vec_new();
    }
    char sep = string[0];
    const char *rest = string + 1;
    int is_ws = 0;

    if (sep != '\n' && is_space_perl(sep)) {
        is_ws = 1;
        while (*rest && is_space_perl(*rest)) rest++;
    }
    return split_core(rest, is_ws, sep, lim);
}

/* ------------------------------------------------------- trace / warning */

static FILE *TraceFP = NULL;

static void trace_line(const char *type, const char *text, const char *level)
{
    if (!TraceFP) return;
    const char *file = cur_file();
    long line = *line_of(file, 1);
    char lineno[32];
    snprintf(lineno, sizeof lineno, "%03ld", line);
    const char *lev = level ? level : "-";

    if (strcmp(LastTraceFile, file) != 0) {
        fprintf(TraceFP, "%s:F:%s: %s:\n", lineno, lev, file);
        LastTraceFile = file;
    }
    /* trace `text` line by line */
    const char *p = text ? text : "";
    int first = 1;
    const char *mo = macroOpen;
    while (1) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (type[0] == 'M') {
            if (!mo[0]) fputc('\n', TraceFP);
            fprintf(TraceFP, "%.*s%s:%s: %s%.*s", (int)strlen(lineno), lineno,
                    type, lev, mo, (int)n, p);
            mo = "";
        } else if (type[0] == 'L' || type[0] == 'D') {
            fprintf(TraceFP, "%.*s%s:%s: %.*s\n", (int)strlen(lineno), lineno,
                    type, lev, (int)n, p);
        }
        if (!nl) break;
        if (!lev[0] || strcmp(lev, "-") == 0) {
            /* the original only increments the line when level is absent */
        } else {
            /* leading zeros padded number: increment */
            long v = atol(lineno) + 1;
            snprintf(lineno, sizeof lineno, "%03ld", v);
        }
        p = nl + 1;
        first = 0;
    }
    (void)first;
    if (type[0] == 'M') fprintf(TraceFP, "%s\n", macroClose);
}

/* Jamal's Error(): print to stderr, and if -d is in effect also return the
 * text so it ends up in the generated output. */
static char *Error(const char *message, const char *input)
{
    SB b;
    sb_init(&b);
    char *trunc = NULL;
    const char *param = input ? input : "";
    if (input && strlen(input) > 20) {
        trunc = xmemdup(input, 20);
        SB t; sb_init(&t); sb_puts(&t, trunc); sb_puts(&t, " ...");
        free(trunc); trunc = sb_detach(&t);
        param = trunc;
    }
    /* the parameter has newlines rewritten as \n */
    SB esc; sb_init(&esc);
    for (const char *p = param; *p; p++) {
        if (*p == '\n') sb_puts(&esc, "\\n"); else sb_putc(&esc, *p);
    }
    sb_puts(&b, message ? message : "");
    sb_puts(&b, "\n");
    if (input && input[0]) {
        sb_puts(&b, "    Parameter: "); sb_puts(&b, esc.p); sb_puts(&b, "\n");
    }
    if (FileStack->n > 0) {
        const char *file = cur_file();
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%ld", *line_of(file, 1));
        sb_puts(&b, "    in file "); sb_puts(&b, file);
        sb_puts(&b, " at line ");  sb_puts(&b, tmp); sb_puts(&b, "\n");
    }
    /* the original prints package/file/line of the caller; the file name is
     * stable for our purposes and the package is always main */
    sb_puts(&b, "    source=>jamal.pl , package=>main , line=>0\n");

    fputs(b.p, stderr);
    if (TraceFP) fputs(b.p, TraceFP);

    sb_free(&esc);
    free(trunc);
    if (!OutputError) { sb_free(&b); return xstrdup(""); }

    SB r; sb_init(&r);
    sb_puts(&r, "\n*** ERROR ***\n");
    sb_puts(&r, b.p);
    sb_puts(&r, "*************");
    sb_free(&b);
    return sb_detach(&r);
}

static char *Warning(const char *message, const char *input)
{
    if (SupressWarnings) return xstrdup("");
    return Error(message, input);
}

/* ----------------------------------------------------------- file helpers */

/* Jamal's Relative(): resolve `rel` against the directory of `ref`. */
static char *Relative(const char *ref, const char *rel)
{
    if (!rel) rel = "";
    if (rel[0] == '/' || rel[0] == '~' || rel[0] == '\\' ||
        (isalpha((unsigned char)rel[0]) && rel[1] == ':'))
        return xstrdup(rel);

    char *r = xstrdup(rel);
    if (r[0] == '.' && r[1] == '/') {
        memmove(r, r + 2, strlen(r + 2) + 1);
    }

    SB path;
    sb_init(&path);
    const char *slash = NULL;
    for (const char *p = ref; *p; p++) if (*p == '/') slash = p;
    if (slash) {
        sb_putn(&path, ref, (size_t)(slash - ref) + 1);
    }
    sb_puts(&path, r);
    free(r);

    char *joined = sb_detach(&path);
    /* collapse duplicate slashes */
    SB col;
    sb_init(&col);
    for (const char *p = joined; *p; p++) {
        if (p[0] == '/' && p[1] == '/') continue;
        sb_putc(&col, *p);
    }
    free(joined);
    joined = sb_detach(&col);

    /* remove all "dir/.." fragments, as the original does */
    Vec *dlist = split_core(joined, 0, '/', -1);
    free(joined);
    Vec *d = vec_new();
    int idx = 0;
    while (idx < dlist->n) {
        int left = dlist->n - idx;
        if (left == 1) { vec_push(d, xstrdup(dlist->v[idx])); idx++; break; }
        if (strcmp(dlist->v[idx], "..") != 0 && strcmp(dlist->v[idx + 1], "..") == 0) {
            idx += 2;
        } else {
            vec_push(d, xstrdup(dlist->v[idx]));
            idx++;
        }
    }
    vec_free(dlist);

    SB out;
    sb_init(&out);
    for (int i = 0; i < d->n; i++) {
        if (i) sb_putc(&out, '/');
        sb_puts(&out, d->v[i]);
    }
    vec_free(d);
    return sb_detach(&out);
}

/* Jamal's maked(): create the directory hierarchy of a file name. */
static int maked(const char *directory)
{
    if (!directory || !directory[0]) return 1;

    char *dir = xstrdup(directory);
    for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';

    /* drop the trailing file name */
    char *lastSlash = strrchr(dir, '/');
    if (!lastSlash) { free(dir); return 1; }
    *lastSlash = '\0';
    if (dir[0] == '\0') { free(dir); return 1; }

    char tmp[4096];
    size_t n = strlen(dir);
    if (n >= sizeof tmp) { free(dir); return 1; }
    memcpy(tmp, dir, n + 1);
    free(dir);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
    return 1;
}

/* -------------------------------------------------------------- DefineMacro */

static void trace_D(const char *name, const char *args, const char *value)
{
    if (!TraceFP) return;
    SB b; sb_init(&b);
    sb_puts(&b, name);
    sb_putc(&b, '(');
    sb_puts(&b, args ? args : "");
    sb_puts(&b, ")=");
    sb_puts(&b, value ? value : "");
    trace_line("D", b.p, NULL);
    sb_free(&b);
}

/* Jamal's DefineMacro().  `args` is the raw formal parameter list (may be
 * empty) and `value` the macro body.  A redefinition without {#undef} saves
 * the previous body so {#restore} can bring it back. */
static void DefineMacro(const char *name, const char *args, const char *value)
{
    if (!name) name = "";
    if (!args) args = "";
    if (!value) value = "";

    MacroEntry *e = macro_find(name);

    if (e && e->value) {
        /* save the old value and its argument list */
        Vec *oldArgs = NULL;
        if (e->args) {
            oldArgs = vec_new();
            for (int i = 0; i < e->args->n; i++) vec_push(oldArgs, xstrdup(e->args->v[i]));
        }
        stack_push_saved(name, xstrdup(e->value), oldArgs);
    }

    if (!e) {
        if (nMacros == capMacros) {
            capMacros = capMacros ? capMacros * 2 : 64;
            Macros = xrealloc(Macros, (size_t)capMacros * sizeof *Macros);
        }
        e = &Macros[nMacros++];
        e->name = xstrdup(name);
        e->value = NULL;
        e->args = NULL;
    }

    if (args[0]) {
        Vec *argv = Split(args, INT_MIN);
        for (int i = 0; i < argv->n; i++) {
            char *q = p_quotemeta(argv->v[i]);
            free(argv->v[i]);
            argv->v[i] = q;
        }
        vec_free(e->args);
        e->args = argv;
    } else {
        vec_free(e->args);
        e->args = NULL;
    }

    free(e->value);
    e->value = xstrdup(value);

    trace_D(name, args, value);
}

static void macro_delete(const char *name)
{
    MacroEntry *e = macro_find(name);
    if (!e) return;
    free(e->value);
    e->value = NULL;
    vec_free(e->args);
    e->args = NULL;
}

static const char *macro_value(const char *name)
{
    MacroEntry *e = macro_find(name);
    return e ? e->value : NULL;
}

/* ------------------------------------------------------- date and time */

static void convertTimeDate(const char *format, time_t when, SB *out)
{
    struct tm tmv;
    struct tm *t = localtime_r(&when, &tmv);
    int year = 1900 + t->tm_year;
    int mon = t->tm_mon + 1;
    int hour = t->tm_hour;
    int min = t->tm_min;
    int sec = t->tm_sec;
    int mday = t->tm_mday;
    int wday = t->tm_wday;

    const char *amv = (hour < 12) ? AM : PM;

    /* Perl applies the substitutions in this exact order on a buffer */
    char *buf = xstrdup(format ? format : "");

#define SUBST(lit, valstr)                                                   \
    do {                                                                     \
        SB nb; sb_init(&nb);                                                 \
        const char *p = buf;                                                 \
        size_t ll = strlen(lit);                                             \
        while (*p) {                                                         \
            if (strncmp(p, lit, ll) == 0) { sb_puts(&nb, valstr); p += ll; }  \
            else sb_putc(&nb, *p++);                                         \
        }                                                                    \
        free(buf); buf = sb_detach(&nb);                                     \
    } while (0)

    char tmp[64];
    snprintf(tmp, sizeof tmp, "%d", hour); SUBST("HH", tmp);
    if (hour < 10) { snprintf(tmp, sizeof tmp, "0%d", hour); } else snprintf(tmp, sizeof tmp, "%d", hour);
    SUBST("0H", tmp);
    int h12 = hour;
    if (h12 > 12) h12 -= 12;
    snprintf(tmp, sizeof tmp, "%d", h12); SUBST("hh", tmp);
    if (h12 < 10) snprintf(tmp, sizeof tmp, "0%d", h12); else snprintf(tmp, sizeof tmp, "%d", h12);
    SUBST("0h", tmp);

    SUBST("am", amv);
    SUBST("pm", amv);

    snprintf(tmp, sizeof tmp, "%d", min); SUBST("mm", tmp);
    if (min < 10) snprintf(tmp, sizeof tmp, "0%d", min);
    SUBST("0m", tmp);

    snprintf(tmp, sizeof tmp, "%d", sec); SUBST("ss", tmp);
    if (sec < 10) snprintf(tmp, sizeof tmp, "0%d", sec);
    SUBST("0s", tmp);

    snprintf(tmp, sizeof tmp, "%d", year); SUBST("YEAR", tmp);
    snprintf(tmp, sizeof tmp, "%d", t->tm_year); SUBST("YY", tmp);

    snprintf(tmp, sizeof tmp, "%d", mon); SUBST("MM", tmp);
    if (mon < 10) snprintf(tmp, sizeof tmp, "0%d", mon);
    SUBST("0M", tmp);
    const char *mname = vec_get(MonthNames, mon - 1);
    SUBST("MONTH", mname ? mname : "");

    snprintf(tmp, sizeof tmp, "%d", mday); SUBST("DD", tmp);
    if (mday < 10) snprintf(tmp, sizeof tmp, "0%d", mday);
    SUBST("0D", tmp);

    int w = wday ? wday : 7;
    snprintf(tmp, sizeof tmp, "%d", w); SUBST("WD", tmp);
    const char *wname = vec_get(DayNames, w - 1);
    SUBST("DAY", wname ? wname : "");

#undef SUBST

    sb_puts(out, buf);
    free(buf);
}

/* Jamal's PredefineMacro(): version, reference, timeNow and the date/time
 * formatting defaults. */
static void PredefineMacro(void)
{
    WeAreInLoop = 0;
    LoopMacroStack = vec_new();

    DefineMacro("version", "", JAMAL_VERSION);
    DefineMacro("reference", "",
        "This page was created using <a href=\"http://peter.verhas.com/progs/perl/jamal\">jamal</A> v" JAMAL_VERSION);
    {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%ld", (long)timeNow);
        DefineMacro("timeNow", "", tmp);
    }

    MonthNames = vec_new();
    const char *months[] = { "January","February","March","April","May","June",
                             "July","August","September","October","November","December" };
    for (int i = 0; i < 12; i++) vec_push(MonthNames, xstrdup(months[i]));

    DayNames = vec_new();
    const char *days[] = { "Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday" };
    for (int i = 0; i < 7; i++) vec_push(DayNames, xstrdup(days[i]));

    AM = xstrdup("am");
    PM = xstrdup("pm");
    TimeFormat = xstrdup("hh:mm:ss");
    DateFormat = xstrdup("MONTH DD, YEAR.");
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

/* remove the backslash-newline continuations the way DoFile does */
static char *strip_continuations(char *s)
{
    SB b;
    sb_init(&b);
    size_t i = 0, len = strlen(s);
    while (i < len) {
        if (s[i] == '\\' && i + 1 < len && s[i + 1] == '\n') {
            size_t j = i + 2;
            if (j < len && s[j] == '_') {
                /* \ + newline + _ + whitespace*  all disappear */
                j++;
                while (j < len && is_space_perl(s[j])) j++;
                i = j;
                continue;
            }
            i += 2; /* plain backslash-newline */
            continue;
        }
        sb_putc(&b, s[i++]);
    }
    char *r = sb_detach(&b);
    free(s);
    return r;
}

static char *DoInput(const char *input, const char *file, int level);
static char *EvalMacro(const char *input, const char *refFile, int level);
static char *DoFile(const char *file);

/* Jamal's ChopMacro(): consume one balanced macro body (delimiters excluded)
 * from *pp and advance *pp past the closing delimiter. */
static char *chopMacro(const char **pp)
{
    const char *p = *pp;
    SB out;
    sb_init(&out);
    int counter = 1;
    size_t olen = strlen(macroOpen);
    size_t clen = strlen(macroClose);

    while (counter) {
        if (*p == '\0') {
            char *r = sb_detach(&out);
            char *err = Error("Erroneous macro nesting.", r);
            free(err);
            *pp = p;
            return r;
        }
        if (strncmp(p, macroOpen, olen) == 0) {
            p += olen;
            counter++;
            sb_puts(&out, macroOpen);
        } else if (strncmp(p, macroClose, clen) == 0) {
            p += clen;
            counter--;
            if (counter <= 0) break;
            sb_puts(&out, macroClose);
        } else {
            const char *a = strstr(p, macroOpen);
            const char *b = strstr(p, macroClose);
            const char *which = NULL;
            if (a && b) which = (b < a) ? b : a;
            else if (a) which = a;
            else if (b) which = b;
            if (which) {
                sb_putn(&out, p, (size_t)(which - p));
                p = which;
            } else {
                sb_puts(&out, p);
                p += strlen(p);
            }
        }
    }
    *pp = p;
    return sb_detach(&out);
}

/* Jamal's DoInput(): walk the text, process every macro, return the result. */
static char *DoInput(const char *input, const char *file, int level)
{
    SB out;
    sb_init(&out);
    char *buf = xstrdup(input);

    while (buf[0]) {
        /* {#sep} can change the delimiters while this loop runs, so the length
         * is read again on every iteration just like the original does. */
        size_t olen = strlen(macroOpen);
        if (strncmp(buf, macroOpen, olen) == 0) {
            memmove(buf, buf + olen, strlen(buf + olen) + 1);
            if (!level) *line_of(file, 1) += (long)mopenCNL;

            const char *p = buf;
            char *macro = chopMacro(&p);
            memmove(buf, p, strlen(p) + 1);

            trace_line("M", macro, NULL);
            long lineNR = *line_of(file, 1) + (long)cnl(macro) + (long)mcloseCNL;

            if (macro[0] != '@') {
                long oldLine = *line_of(file, 1);
                char *sub = DoInput(macro, file, level);
                free(macro);
                macro = sub;
                *line_of(file, 1) = oldLine;
                trace_line("M", macro, NULL);
            }

            char *res = EvalMacro(macro, file, level);
            sb_puts(&out, res);
            free(res);
            free(macro);

            if (!level) *line_of(file, 1) = lineNR;
        } else {
            const char *i = strstr(buf, macroOpen);
            if (i) {
                char *seg = xmemdup(buf, (size_t)(i - buf));
                sb_puts(&out, seg);
                if (!level) *line_of(file, 1) += (long)cnl(seg);
                free(seg);
                memmove(buf, buf + (i - buf), strlen(i) + 1);
            } else {
                sb_puts(&out, buf);
                if (!level) *line_of(file, 1) += (long)cnl(buf);
                buf[0] = '\0';
            }
        }
    }
    free(buf);
    return sb_detach(&out);
}

/* Jamal's DoFile(): read a file, drop line continuations, process it. */
static char *DoFile(const char *file)
{
    Vec *fs = FileStack;
    vec_push(fs, xstrdup(file));
    *line_of(file, 1) = 1;

    int ok = 0;
    char *content = read_whole_file(file, &ok);
    if (!ok) {
        fprintf(stderr, "Input file '%s' cannot be opened.\n", file);
        if (fs->n > 1) {
            fprintf(stderr, " Include hierarchy leading to this file name:\n");
            if (fs->n > 0) free(fs->v[--fs->n]);
            char *lastFile = (fs->n > 0) ? fs->v[--fs->n] : NULL;
            int spc = 0;
            for (int i = 0; i < fs->n; i++) {
                fprintf(stderr, "%*s file %s at line %ld included\n",
                        spc, "", fs->v[i], *line_of(fs->v[i], 1));
                spc++;
            }
            fprintf(stderr, "%*s file %s at line %ld tried to include this file\n",
                    spc, "", lastFile ? lastFile : "",
                    *line_of(lastFile ? lastFile : "", 1));
            /* The original pops this entry without keeping a reference; here it
             * belongs to nobody any more, so it can go. */
            free(lastFile);
        }
        /* The original pops blindly: the stack can already be empty (it is
         * desynchronised by the branch above), so guard the accesses. */
        if (fs->n > 0) free(fs->v[--fs->n]);
        return xstrdup("");
    }

    content = strip_continuations(content);
    char *res = DoInput(content, file, 0);
    free(content);
    if (fs->n > 0) free(fs->v[--fs->n]);
    return res;
}

/* ============================================================ EvalMacro == */

static int p_isword(char c) { return isalnum((unsigned char)c) || c == '_'; }

static int starts_with(const char *s, const char *pre)
{
    return strncmp(s, pre, strlen(pre)) == 0;
}

/* {#define ...} -- parse the regex
 *   ^define\s*(?:(\?)\s*|\s+)(\w+)?((?:::\w+)*)((?:.|\n)*?)=((?:.|\n)*)$
 * Returns ok=0 when the text is not a define at all. */
typedef struct {
    int ok, conditional;
    const char *base; size_t baseLen;
    const char *module; size_t moduleLen;
    const char *param; size_t paramLen;
    const char *value;
} DefineParse;

static DefineParse parse_define(const char *s)
{
    DefineParse d;
    memset(&d, 0, sizeof d);
    if (!starts_with(s, "define")) return d;

    const char *p = s + 6;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (*q == '?') {
        d.conditional = 1;
        q++;
        while (is_space_perl(*q)) q++;
        p = q;
    } else {
        if (q == p) return d;   /* the `\s+` alternative needs whitespace */
        p = q;
    }

    const char *b = p;
    while (p_isword(*p)) p++;
    d.base = b; d.baseLen = (size_t)(p - b);

    const char *m = p;
    while (p[0] == ':' && p[1] == ':') {
        const char *r = p + 2;
        if (!p_isword(*r)) break;
        while (p_isword(*r)) r++;
        p = r;
    }
    d.module = m; d.moduleLen = (size_t)(p - m);

    const char *eq = strchr(p, '=');
    if (!eq) return d;               /* the regex requires the '=' */
    d.param = p; d.paramLen = (size_t)(eq - p);
    d.value = eq + 1;
    d.ok = 1;
    return d;
}

/* Evaluate the operational macros ({#+ ...}, {#= ...}, {#-f ...}, ...). */
static char *eval_operators(const char *op, const char *macro,
                            const char *refFile, int level, int *matched)
{
    *matched = 1;

    Vec *list = Split(macro, INT_MIN);

    char *fop;
    if (list->n > 0) {
        fop = list->v[0];
        for (int i = 1; i < list->n; i++) list->v[i - 1] = list->v[i];
        list->n--;
    } else {
        fop = xstrdup("");
    }

    /* ---- unary file operators -z -s -d -f -e -t (lowercase = input base) */
    {
        char o1 = op[0], o2 = op[1];
        if (o1 == '-' && o2 && strchr("zsdfet", tolower((unsigned char)o2))) {
            int lower = (o2 == (char)tolower((unsigned char)o2));
            char *path = (lower || !Destination)
                       ? Relative(refFile, fop)
                       : Relative(Destination, fop);
            char oc = (char)tolower((unsigned char)o2);
            struct stat st;
            int have = (stat(path, &st) == 0);
            char *ret = NULL;
            char tmp[64];
            if (oc == 'z') {
                ret = xstrdup((have && st.st_size == 0) ? "1" : "0");
            } else if (oc == 's') {
                if (!have) ret = xstrdup("0");
                else { snprintf(tmp, sizeof tmp, "%lld", (long long)st.st_size); ret = xstrdup(tmp); }
            } else if (oc == 'd') {
                ret = xstrdup((have && S_ISDIR(st.st_mode)) ? "1" : "0");
            } else if (oc == 'f') {
                ret = xstrdup((have && S_ISREG(st.st_mode)) ? "1" : "0");
            } else if (oc == 'e') {
                ret = xstrdup(have ? "1" : "0");
            } else { /* t */
                snprintf(tmp, sizeof tmp, "%lld", (long long)(have ? (long long)st.st_mtime : 0));
                ret = xstrdup(tmp);
            }
            free(path); free(fop); vec_free(list);
            return ret;
        }
    }

    /* ---- unary minus / plus (single operand only) */
    if (list->n == 0 && strcmp(op, "-") == 0) {
        char t[64]; p_numstr(t, sizeof t, -1 * p_num(fop));
        free(fop); vec_free(list); return xstrdup(t);
    }
    if (list->n == 0 && strcmp(op, "+") == 0) {
        char t[64]; p_numstr(t, sizeof t, 1 * p_num(fop));
        free(fop); vec_free(list); return xstrdup(t);
    }

    /* ---- not */
    if (strcmp(op, "not") == 0) {
        int r = p_true(fop) ? 0 : 1;
        if (list->n == 0 || p_true(fop)) {
            char *s = xstrdup(r ? "1" : "0");
            free(fop); vec_free(list); return s;
        }
    }

    if (strcmp(op, "or") == 0 && p_true(fop))  { free(fop); vec_free(list); return xstrdup("1"); }
    if (strcmp(op, "and") == 0 && !p_true(fop)) { free(fop); vec_free(list); return xstrdup("0"); }

    if (list->n == 0) {
        char *e = Error("Operational macro has less than two arguments:", op);
        free(e);
    }

    /* ---- the binary operator loop */
    char *result = xstrdup(fop);
    double d = p_num(fop);
    int numeric = 0;   /* has `result` been overwritten numerically? */
    int early = 0;
    char earlyBuf[64] = {0};

    for (int i = 0; i < list->n && !early; i++) {
        const char *a = list->v[i];
        if      (strcmp(op, "+") == 0) { d += p_num(a); numeric = 1; }
        else if (strcmp(op, "-") == 0) { d -= p_num(a); numeric = 1; }
        else if (strcmp(op, "*") == 0) { d *= p_num(a); numeric = 1; }
        else if (strcmp(op, "/") == 0) {
            /* Perl dies here when the divisor is zero ("Illegal division by
             * zero at jamal.pl line 912.").  The text, source name and line
             * number are what the original prints; only the exit status is
             * rounded to the 2 that a failing run shows. */
            if (p_num(a) == 0.0) {
                fputs("Illegal division by zero at jamal.pl line 912.\n", stderr);
                exit(2);
            }
            d /= p_num(a);
            numeric = 1;
        }
        else if (strcmp(op, "=") == 0)  { numeric = 1; d = 1; if (p_num(fop) != p_num(a)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "!=") == 0) { numeric = 1; d = 1; if (p_num(fop) == p_num(a)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "<") == 0)  { numeric = 1; d = 1; if (!(p_num(fop) <  p_num(a))) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, ">") == 0)  { numeric = 1; d = 1; if (!(p_num(fop) >  p_num(a))) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "<=") == 0) { numeric = 1; d = 1; if (!(p_num(fop) <= p_num(a))) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, ">=") == 0) { numeric = 1; d = 1; if (!(p_num(fop) >= p_num(a))) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "eq") == 0) { numeric = 1; d = 1; if (strcmp(fop, a) != 0) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "ne") == 0) { numeric = 1; d = 1; if (strcmp(fop, a) == 0) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "lt") == 0) { numeric = 1; d = 1; if (!(strcmp(fop, a) <  0)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "gt") == 0) { numeric = 1; d = 1; if (!(strcmp(fop, a) >  0)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "le") == 0) { numeric = 1; d = 1; if (!(strcmp(fop, a) <= 0)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "ge") == 0) { numeric = 1; d = 1; if (!(strcmp(fop, a) >= 0)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "and") == 0) { numeric = 1; d = 1; if (!p_true(a)) { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "not") == 0) { numeric = 1; d = 1; if (p_true(a))  { early = 1; strcpy(earlyBuf, "0"); } }
        else if (strcmp(op, "or") == 0)  { numeric = 1; d = 0; if (p_true(a))  { early = 1; strcpy(earlyBuf, "1"); } }
    }

    free(result);
    result = NULL;
    free(fop);
    vec_free(list);

    if (early) return xstrdup(earlyBuf);

    {
        char t[64];
        if (numeric) p_numstr(t, sizeof t, d);
        else snprintf(t, sizeof t, "%s", "");   /* unreachable: every op is handled */
        return DoInput(t, refFile, level + 1);
    }
}

/* drop one optional leading and one optional trailing double quote */
static char *strip_optional_quotes(const char *s)
{
    size_t n = strlen(s);
    size_t a = 0;
    if (n > 0 && s[0] == '"') { a = 1; n--; }
    if (n > 0 && s[a + n - 1] == '"') n--;
    return xmemdup(s + a, n);
}

/* =============================================================== EvalMacro */

/* Perl's `&Error(...); exit;` -- print the diagnostic and stop with status 0
 * exactly like the original bare `exit`. */
static void fatal(const char *message, const char *input)
{
    char *m = Error(message, input);
    free(m);
    exit(0);
}

/* Perl /^PREFIX(\W...)$/: the byte right after PREFIX must be a non-word byte. */
static int after_prefix_nonword(const char *s, const char *prefix, const char **rest)
{
    size_t pl = strlen(prefix);
    if (strncmp(s, prefix, pl) != 0) return 0;
    const char *p = s + pl;
    if (!*p || p_isword(*p)) return 0;
    *rest = p;
    return 1;
}

/* Perl /^KEYWORD\s+"?(.*?)"?$/ evaluated with `s` at the keyword.  `\s+` is
 * greedy and both quotes are optional, so the capture is the rest of the string
 * with one optional leading and one optional trailing double quote removed.
 * This is exactly what the original `include` macros rely on. */
static int match_ws_name(const char *s, const char *keyword, char **file)
{
    size_t kl = strlen(keyword);
    if (strncmp(s, keyword, kl) != 0) return 0;
    const char *p = s + kl;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (q == p) return 0;                      /* \s+ needs at least one byte */
    *file = strip_optional_quotes(q);
    return 1;
}

/* the closing counterpart of the first byte, as a plain byte (CharPair()
 * returns it quotemeta'd because the original splices it into a regex) */
static char char_pair_char(char ch)
{
    switch (ch) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        default:  return ch;
    }
}

/* replace every occurrence of the literal `find` with `with` ($wp =~ s/x/y/g) */
static char *str_replace_all(const char *s, const char *find, const char *with)
{
    SB out; sb_init(&out);
    size_t fl = strlen(find);
    const char *p = s;
    while (*p) {
        if (fl && strncmp(p, find, fl) == 0) { sb_puts(&out, with); p += fl; }
        else sb_putc(&out, *p++);
    }
    return sb_detach(&out);
}

/* {-a fragment of a macro body plus a "may still be searched" flag -} */
typedef struct {
    char *frag;
    int   resolv;
} Frag;

static void frag_push(Frag **v, int *n, int *cap, char *frag, int resolv)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *v = xrealloc(*v, (size_t)*cap * sizeof **v);
    }
    (*v)[*n].frag = frag;
    (*v)[*n].resolv = resolv;
    (*n)++;
}

/* The formal argument substitution of the original.  Text that came from an
 * actual argument is marked "resolved" and is never searched again for formal
 * parameters -- the behaviour the long comment in the Perl source describes. */
static char *expand_formals(const char *value, Vec *formal, Vec *argv)
{
    Frag *cur = NULL; int ncur = 0, capcur = 0;
    frag_push(&cur, &ncur, &capcur, xstrdup(value), 1);

    for (int i = 0; i < formal->n; i++) {
        const char *sear = formal->v[i];            /* already quotemeta'd */
        const char *repla = (i < argv->n) ? argv->v[i] : NULL;
        if (!*sear) break;                          /* the original loops forever */

        Frag *next = NULL; int nnext = 0, capnext = 0;
        for (int j = 0; j < ncur; j++) {
            if (!cur[j].resolv) {
                frag_push(&next, &nnext, &capnext, cur[j].frag, 0);
                continue;
            }
            char *item = cur[j].frag;
            while (item && *item) {
                char *hit = strstr(item, sear);
                if (!hit) break;
                if (hit != item)
                    frag_push(&next, &nnext, &capnext, xmemdup(item, (size_t)(hit - item)), 1);
                if (repla && *repla)
                    frag_push(&next, &nnext, &capnext, xstrdup(repla), 0);
                item = hit + strlen(sear);
            }
            if (item && *item)
                frag_push(&next, &nnext, &capnext, xstrdup(item), 1);
            free(cur[j].frag);
        }
        free(cur);
        cur = next; ncur = nnext; capcur = capnext;
    }

    SB out; sb_init(&out);
    for (int i = 0; i < ncur; i++) sb_puts(&out, cur[i].frag);
    for (int i = 0; i < ncur; i++) free(cur[i].frag);
    free(cur);
    return sb_detach(&out);
}

/* {#include pre}: the file with < and > escaped */
static char *include_pre(const char *refFile, const char *name)
{
    char *file = Relative(refFile, name);
    int ok = 0;
    char *content = read_whole_file(file, &ok);
    if (!ok) {
        char msg[512];
        snprintf(msg, sizeof msg, "File %s can not be read.", file);
        fatal(msg, file);
    }
    SB out; sb_init(&out);
    for (const char *p = content; *p; p++) {
        if (*p == '<') sb_puts(&out, "&lt;");
        else if (*p == '>') sb_puts(&out, "&gt;");
        else sb_putc(&out, *p);
    }
    free(file);
    free(content);
    return sb_detach(&out);
}

/* {#include verbatim} */
static char *include_verbatim(const char *refFile, const char *name)
{
    char *file = Relative(refFile, name);
    int ok = 0;
    char *content = read_whole_file(file, &ok);
    if (!ok) {
        char msg[512];
        /* the typo is in the original */
        snprintf(msg, sizeof msg, "File %s cann ot be read.", file);
        fatal(msg, file);
    }
    free(file);
    return content;
}

/* Perl /^\s*((?:\+|\-)?\d+)\s*\.\.\.?\s*((?:\+|\-)?\d+)\s*$/ */
static int numeric_range(const char *s, char **from, char **to)
{
    const char *p = s;
    while (is_space_perl(*p)) p++;
    const char *a = p;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    const char *aEnd = p;
    while (is_space_perl(*p)) p++;
    if (p[0] != '.' || p[1] != '.') return 0;
    p += 2;
    if (*p == '.') p++;
    while (is_space_perl(*p)) p++;
    const char *b = p;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    const char *bEnd = p;
    while (is_space_perl(*p)) p++;
    if (*p) return 0;
    *from = xmemdup(a, (size_t)(aEnd - a));
    *to   = xmemdup(b, (size_t)(bEnd - b));
    return 1;
}

/* Perl /^(\$(?:\$|\_|\w)+)$/ -- the loop variables filled in by {#dir} */
static int is_loop_macro(const char *s)
{
    const char *p = s;
    if (*p != '$') return 0;
    p++;
    size_t n = 0;
    while (*p == '$' || *p == '_' || p_isword(*p)) { p++; n++; }
    return *p == '\0' && n > 0;
}

/* Perl /^undef\s+(\w+)\s*$/ */
static int match_undef(const char *s, char **name)
{
    if (strncmp(s, "undef", 5) != 0) return 0;
    const char *p = s + 5;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (q == p) return 0;
    const char *a = q;
    while (p_isword(*q)) q++;
    if (q == a) return 0;
    const char *b = q;
    while (is_space_perl(*q)) q++;
    if (*q) return 0;
    *name = xmemdup(a, (size_t)(b - a));
    return 1;
}

/* Perl /^INC\s+(.*)$/ : the rest of the line with trailing whitespace removed */
static int match_inc(const char *s, char **dir)
{
    if (strncmp(s, "INC", 3) != 0) return 0;
    const char *p = s + 3;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (q == p) return 0;
    const char *nl = strchr(q, '\n');
    if (nl && nl[1] != '\0') return 0;    /* `$` only accepts a final newline */
    size_t n = nl ? (size_t)(nl - q) : strlen(q);
    while (n > 0 && is_space_perl(q[n - 1])) n--;
    *dir = xmemdup(q, n);
    return 1;
}

/* Perl /^restore\s+(\w+)\s*$/ */
static int match_restore(const char *s, char **name)
{
    if (strncmp(s, "restore", 7) != 0) return 0;
    const char *p = s + 7;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (q == p) return 0;
    const char *a = q;
    while (p_isword(*q)) q++;
    if (q == a) return 0;
    const char *b = q;
    while (is_space_perl(*q)) q++;
    if (*q) return 0;
    *name = xmemdup(a, (size_t)(b - a));
    return 1;
}

/* Perl /^format\s+(time|date|am|pm)=((?:.|\n)*)$/ */
static int match_format(const char *s, const char **which, const char **value)
{
    if (strncmp(s, "format", 6) != 0) return 0;
    const char *p = s + 6;
    const char *q = p;
    while (is_space_perl(*q)) q++;
    if (q == p) return 0;
    static const char *names[] = { "time", "date", "am", "pm" };
    for (int i = 0; i < 4; i++) {
        size_t l = strlen(names[i]);
        if (strncmp(q, names[i], l) == 0 && q[l] == '=') {
            *which = names[i];
            *value = q + l + 1;
            return 1;
        }
    }
    return 0;
}

/* Perl /^time\s*$/ and /^date\s*$/ */
static int match_bare_word(const char *s, const char *word)
{
    size_t l = strlen(word);
    if (strncmp(s, word, l) != 0) return 0;
    const char *p = s + l;
    while (is_space_perl(*p)) p++;
    return *p == '\0';
}

/* Perl /^(?:\[|\{)\s*$/ and /^(?:\]|\})\s*$/ */
static int match_single_char(const char *s, const char *set)
{
    if (!s[0] || !strchr(set, s[0])) return 0;
    const char *p = s + 1;
    while (is_space_perl(*p)) p++;
    return *p == '\0';
}

/* Perl /^null\s*((?:.|\n)*)$/ and /^comment\s*((?:.|\n)*)$/ */
static int match_swallow(const char *s, const char *word, const char **capture)
{
    size_t l = strlen(word);
    if (strncmp(s, word, l) != 0) return 0;
    const char *p = s + l;
    while (is_space_perl(*p)) p++;
    *capture = p;
    return 1;
}

/* set the body (and the formal parameters) of a macro; {#restore} uses it */
static void macro_set(const char *name, char *value, Vec *args)
{
    MacroEntry *e = macro_find(name);
    if (!e) {
        if (nMacros == capMacros) {
            capMacros = capMacros ? capMacros * 2 : 64;
            Macros = xrealloc(Macros, (size_t)capMacros * sizeof *Macros);
        }
        e = &Macros[nMacros++];
        e->name = xstrdup(name);
        e->value = NULL;
        e->args = NULL;
    }
    free(e->value);
    e->value = value;
    vec_free(e->args);
    e->args = args;
}

/* ============================================================== esd module
 *
 * The texinfo documentation pulls its source code through the `esd' jamal
 * extension module (esd.pm).  esd.pm is a Perl package: jamal loads it with
 * `use jamal::esd' and then evaluates its macro calls as Perl code, so the
 * documentation chain (jamal -> t2h) could not run without a Perl
 * interpreter.  This port implements the same module in C: the macros
 * `esd::command', `esd::nocommand', `esd::include' and `esd::execute', with
 * the substitutions stored by `esd::command' applied to every line of the
 * included file.
 *
 * Two deliberate differences from esd.pm, both documented in OLD/README.md:
 *
 *   - esd.pm keeps its commands in a Perl hash and evals them in `keys
 *     %COMMAND' order, which is a different (unspecified) order on every run.
 *     This port runs them sorted by key, which is the order the shipped files
 *     are written for.
 *   - the substitutions are Perl regular expressions.  Only the subset the
 *     shipped files use is understood here (literals, escapes, `.', `^', `$',
 *     character classes, groups, lazy quantifiers, the `g' flag); a command
 *     using anything else is reported on stderr and left unapplied, where
 *     Perl would silently drop a command that fails to compile.
 */

#define RX_MAXCAP 10
#define RX_REP_MAX 1000

typedef struct RxSeq RxSeq;

typedef struct {
    unsigned char bits[32];
    int neg;
} RxClass;

enum { RX_LIT, RX_ANY, RX_CLASS, RX_BOL, RX_EOL, RX_GROUP };

typedef struct {
    int type;
    unsigned char ch;
    RxClass *cls;
    int cap;                /* capture index, 0 for a non capturing group */
    RxSeq *alts;
    int nalts;
} RxAtom;

typedef struct {
    RxAtom *atom;
    int min, max;           /* max < 0 means unbounded */
    int greedy;
} RxItem;

struct RxSeq {
    RxItem *items;
    int n;
    int cap;
};

typedef struct {
    const char *s;
    size_t len;
    int capstart[RX_MAXCAP], capend[RX_MAXCAP];
} RxState;

typedef struct {
    const char *p;
    int err;
    char why[128];
    int ncap;
} RxParse;

/* ---------------------------------------------------------------- bitmap */

static void rx_setbit(RxClass *c, unsigned char ch)
{
    c->bits[ch >> 3] |= (unsigned char)(1u << (ch & 7));
}

static int rx_getbit(const RxClass *c, unsigned char ch)
{
    return (c->bits[ch >> 3] >> (ch & 7)) & 1;
}

static RxClass *rx_newclass(void)
{
    RxClass *c = xmalloc(sizeof *c);
    memset(c, 0, sizeof *c);
    return c;
}

static void rx_predef(RxClass *c, char e)
{
    for (int i = 0; i < 256; i++) {
        unsigned char ch = (unsigned char)i;
        int hit;
        switch (e) {
        case 's': hit = is_space_perl((char)ch); break;
        case 'S': hit = !is_space_perl((char)ch); break;
        case 'd': hit = isdigit(ch); break;
        case 'D': hit = !isdigit(ch); break;
        case 'w': hit = p_isword((char)ch); break;
        default:  hit = !p_isword((char)ch); break;
        }
        if (hit) rx_setbit(c, ch);
    }
}

/* ---------------------------------------------------------------- parsing */

static void rx_fail(RxParse *ps, const char *why)
{
    if (!ps->err) {
        ps->err = 1;
        snprintf(ps->why, sizeof ps->why, "%s", why);
    }
}

static void rx_pushitem(RxSeq *sq, RxAtom *a, int min, int max, int greedy)
{
    if (sq->n == sq->cap) {
        sq->cap = sq->cap ? sq->cap * 2 : 8;
        sq->items = xrealloc(sq->items, (size_t)sq->cap * sizeof *sq->items);
    }
    sq->items[sq->n].atom = a;
    sq->items[sq->n].min = min;
    sq->items[sq->n].max = max;
    sq->items[sq->n].greedy = greedy;
    sq->n++;
}

static void rx_pushalt(RxAtom *a, RxSeq s)
{
    a->alts = xrealloc(a->alts, (size_t)(a->nalts + 1) * sizeof *a->alts);
    a->alts[a->nalts++] = s;
}

static RxAtom *rx_parse_class(RxParse *ps)
{
    RxAtom *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->type = RX_CLASS;
    a->cls = rx_newclass();
    ps->p++;                                     /* '[' */
    if (*ps->p == '^') { a->cls->neg = 1; ps->p++; }
    int first = 1;
    for (;;) {
        char c = *ps->p;
        if (c == '\0') { rx_fail(ps, "unmatched ["); return a; }
        if (c == ']' && !first) { ps->p++; break; }
        first = 0;
        if (c == '\\') {
            ps->p++;
            char e = *ps->p;
            if (!e) { rx_fail(ps, "trailing backslash in [...]"); return a; }
            ps->p++;
            if (e == 's' || e == 'S' || e == 'd' || e == 'D' || e == 'w' || e == 'W') {
                rx_predef(a->cls, e);
                continue;
            }
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'f': c = '\f'; break;
            case 'a': c = '\a'; break;
            case 'e': c = '\033'; break;
            default:  c = e; break;
            }
        } else ps->p++;
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            ps->p++;
            char hi = *ps->p++;
            if (hi == '\\' && *ps->p) hi = *ps->p++;
            if ((unsigned char)hi < (unsigned char)c) { rx_fail(ps, "invalid range in [...]"); return a; }
            for (int x = (unsigned char)c; x <= (unsigned char)hi; x++) rx_setbit(a->cls, (unsigned char)x);
            continue;
        }
        rx_setbit(a->cls, (unsigned char)c);
    }
    return a;
}

static RxSeq rx_parse_alt(RxParse *ps, int inGroup);

static RxAtom *rx_parse_group(RxParse *ps)
{
    RxAtom *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->type = RX_GROUP;
    ps->p++;                                     /* '(' */
    if (*ps->p == '?') { rx_fail(ps, "(?...): no such construct here"); return a; }
    if (ps->ncap + 1 >= RX_MAXCAP) { rx_fail(ps, "too many capture groups"); return a; }
    a->cap = ++ps->ncap;
    for (;;) {
        RxSeq s = rx_parse_alt(ps, 1);
        rx_pushalt(a, s);
        if (ps->err) return a;
        if (*ps->p == '|') { ps->p++; continue; }
        if (*ps->p == ')') { ps->p++; return a; }
        return a;                                /* '\0': reported by rx_parse_alt */
    }
}

static RxAtom *rx_parse_atom(RxParse *ps)
{
    RxAtom *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    char c = *ps->p;
    switch (c) {
    case '.': ps->p++; a->type = RX_ANY; return a;
    case '^': ps->p++; a->type = RX_BOL; return a;
    case '$':
        /* Perl interpolates `$name', `${name}' and `$(name)' in a pattern;
         * the anchor is the only dollar the esd files use */
        if (p_isword(ps->p[1]) || ps->p[1] == '{' || ps->p[1] == '(') {
            rx_fail(ps, "variable interpolation in a pattern is not supported");
            return a;
        }
        ps->p++;
        a->type = RX_EOL;
        return a;
    case '(': return rx_parse_group(ps);
    case '[': return rx_parse_class(ps);
    case '*': case '+': case '?':
        rx_fail(ps, "quantifier follows nothing");
        return a;
    case '\\': {
        ps->p++;
        char e = *ps->p;
        if (!e) { rx_fail(ps, "trailing backslash"); return a; }
        ps->p++;
        if (e == 's' || e == 'S' || e == 'd' || e == 'D' || e == 'w' || e == 'W') {
            a->type = RX_CLASS;
            a->cls = rx_newclass();
            rx_predef(a->cls, e);
            return a;
        }
        switch (e) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'f': c = '\f'; break;
        case 'a': c = '\a'; break;
        case 'e': c = '\033'; break;
        case 'b': case 'B': case 'A': case 'Z': case 'z': case 'G': case 'Q': case 'E':
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            rx_fail(ps, "unsupported escape in pattern");
            return a;
        default: c = e; break;
        }
        a->type = RX_LIT;
        a->ch = (unsigned char)c;
        return a;
    }
    default:
        a->type = RX_LIT;
        a->ch = (unsigned char)c;
        ps->p++;
        return a;
    }
}

static void rx_parse_quant(RxParse *ps, int *min, int *max, int *greedy)
{
    char c = *ps->p;
    *min = 1; *max = 1; *greedy = 1;
    if (c == '*') { *min = 0; *max = -1; ps->p++; }
    else if (c == '+') { *min = 1; *max = -1; ps->p++; }
    else if (c == '?') { *min = 0; *max = 1; ps->p++; }
    else if (c == '{') {
        const char *q = ps->p + 1;
        int m = 0, hasM = 0;
        while (isdigit((unsigned char)*q)) { m = m * 10 + (*q - '0'); q++; hasM = 1; }
        if (hasM && *q == '}') { *min = *max = m; ps->p = q + 1; }
        else if (hasM && *q == ',') {
            q++;
            if (*q == '}') { *min = m; *max = -1; ps->p = q + 1; }
            else {
                int n = 0, hasN = 0;
                while (isdigit((unsigned char)*q)) { n = n * 10 + (*q - '0'); q++; hasN = 1; }
                if (hasN && *q == '}') { *min = m; *max = n; ps->p = q + 1; }
            }
        }
        /* anything else: Perl treats the brace as a literal, leave it there */
    }
    if ((*min != 1 || *max != 1) && *ps->p == '?' && !ps->err) {
        *greedy = 0;
        ps->p++;
    }
}

static RxSeq rx_parse_alt(RxParse *ps, int inGroup)
{
    RxSeq sq = {NULL, 0, 0};
    for (;;) {
        char c = *ps->p;
        if (ps->err) break;
        if (c == '\0') { if (inGroup) rx_fail(ps, "unmatched ("); break; }
        if (c == '|') break;
        if (c == ')') { if (inGroup) break; rx_fail(ps, "unmatched )"); break; }
        RxAtom *a = rx_parse_atom(ps);
        if (ps->err) break;
        int min, max, greedy;
        rx_parse_quant(ps, &min, &max, &greedy);
        if (ps->err) break;
        rx_pushitem(&sq, a, min, max, greedy);
    }
    return sq;
}

/* compile a pattern into a one item sequence holding the top level group */
static int rx_compile(const char *pat, RxSeq *out, int *ncap, char *err, size_t errsz)
{
    RxParse ps;
    memset(&ps, 0, sizeof ps);
    ps.p = pat;
    RxAtom *top = xmalloc(sizeof *top);
    memset(top, 0, sizeof *top);
    top->type = RX_GROUP;
    for (;;) {
        RxSeq s = rx_parse_alt(&ps, 0);
        rx_pushalt(top, s);
        if (ps.err) break;
        if (*ps.p == '|') { ps.p++; continue; }
        break;
    }
    if (ps.err) {
        snprintf(err, errsz, "%s", ps.why);
        return 0;
    }
    memset(out, 0, sizeof *out);
    rx_pushitem(out, top, 1, 1, 1);
    *ncap = ps.ncap;
    return 1;
}

/* --------------------------------------------------------------- matching
 *
 * Matching is written in continuation passing style: every node is handed the
 * continuation that matches the rest of the pattern, and a node that can match
 * in several ways keeps asking that continuation until it succeeds.  That is
 * what a backtracking engine does internally and it is the only way the lazy
 * quantifiers used by esd.pm (`(.*?)') can backtrack into a longer match when
 * the text that follows them fails. */

static void rx_save(const RxState *st, int *save)
{
    for (int i = 0; i < RX_MAXCAP; i++) {
        save[2 * i] = st->capstart[i];
        save[2 * i + 1] = st->capend[i];
    }
}

static void rx_restore(RxState *st, const int *save)
{
    for (int i = 0; i < RX_MAXCAP; i++) {
        st->capstart[i] = save[2 * i];
        st->capend[i] = save[2 * i + 1];
    }
}

typedef int (*RxCont)(RxState *st, size_t pos, void *ctx);

static int rx_m_seq(const RxSeq *sq, int idx, RxState *st, size_t pos, RxCont cont, void *ctx);
static int rx_m_atom(const RxAtom *a, RxState *st, size_t pos, RxCont cont, void *ctx);

typedef struct {
    const RxSeq *sq;
    int idx;
    RxCont cont;
    void *ctx;
} RxSeqCont;

static int rx_seq_cont(RxState *st, size_t pos, void *vctx)
{
    RxSeqCont *c = vctx;
    return rx_m_seq(c->sq, c->idx, st, pos, c->cont, c->ctx);
}

typedef struct {
    const RxSeq *sq;
    int idx;
    int count;                  /* repetitions completed when this one matched */
    size_t start;               /* where the repetition started */
    RxCont cont;
    void *ctx;
} RxRepCont;

static int rx_m_rep(const RxSeq *sq, int idx, int count, size_t pos, RxState *st,
                    RxCont cont, void *ctx);

static int rx_rep_cont(RxState *st, size_t pos, void *vctx)
{
    RxRepCont *c = vctx;
    const RxItem *it = &c->sq->items[c->idx];
    /* a repetition that consumed nothing would recurse for ever; allow it only
     * while the minimum number of repetitions is not reached yet */
    if (pos == c->start && c->count >= it->min) return 0;
    return rx_m_rep(c->sq, c->idx, c->count, pos, st, c->cont, c->ctx);
}

static int rx_m_rep(const RxSeq *sq, int idx, int count, size_t pos, RxState *st,
                    RxCont cont, void *ctx)
{
    const RxItem *it = &sq->items[idx];
    int max = it->max < 0 ? RX_REP_MAX : it->max;
    int save[2 * RX_MAXCAP];

    if (it->greedy) {
        if (count < max) {
            RxRepCont rc = { sq, idx, count + 1, pos, cont, ctx };
            rx_save(st, save);
            if (rx_m_atom(it->atom, st, pos, rx_rep_cont, &rc)) return 1;
            rx_restore(st, save);
        }
        if (count >= it->min) return cont(st, pos, ctx);
        return 0;
    }
    if (count >= it->min) {
        rx_save(st, save);
        if (cont(st, pos, ctx)) return 1;
        rx_restore(st, save);
    }
    if (count < max) {
        RxRepCont rc = { sq, idx, count + 1, pos, cont, ctx };
        rx_save(st, save);
        if (rx_m_atom(it->atom, st, pos, rx_rep_cont, &rc)) return 1;
        rx_restore(st, save);
    }
    return 0;
}

static int rx_m_seq(const RxSeq *sq, int idx, RxState *st, size_t pos, RxCont cont, void *ctx)
{
    if (idx >= sq->n) return cont(st, pos, ctx);
    const RxItem *it = &sq->items[idx];
    RxSeqCont c = { sq, idx + 1, cont, ctx };
    if (it->min == 1 && it->max == 1) return rx_m_atom(it->atom, st, pos, rx_seq_cont, &c);
    return rx_m_rep(sq, idx, 0, pos, st, rx_seq_cont, &c);
}

typedef struct {
    const RxAtom *a;
    size_t start;
    RxCont cont;
    void *ctx;
} RxGroupCont;

static int rx_group_cont(RxState *st, size_t pos, void *vctx)
{
    RxGroupCont *g = vctx;
    if (g->a->cap) {
        st->capstart[g->a->cap] = (int)g->start;
        st->capend[g->a->cap] = (int)pos;
    }
    if (g->cont(st, pos, g->ctx)) return 1;
    if (g->a->cap) { st->capstart[g->a->cap] = -1; st->capend[g->a->cap] = -1; }
    return 0;
}

static int rx_m_atom(const RxAtom *a, RxState *st, size_t pos, RxCont cont, void *ctx)
{
    switch (a->type) {
    case RX_LIT:
        if (pos < st->len && (unsigned char)st->s[pos] == a->ch) return cont(st, pos + 1, ctx);
        return 0;
    case RX_ANY:
        if (pos < st->len && st->s[pos] != '\n') return cont(st, pos + 1, ctx);
        return 0;
    case RX_CLASS: {
        if (pos >= st->len) return 0;
        if (rx_getbit(a->cls, (unsigned char)st->s[pos]) == a->cls->neg) return 0;
        return cont(st, pos + 1, ctx);
    }
    case RX_BOL:
        if (pos == 0) return cont(st, pos, ctx);
        return 0;
    case RX_EOL:
        if (pos == st->len || (pos + 1 == st->len && st->s[pos] == '\n')) return cont(st, pos, ctx);
        return 0;
    case RX_GROUP: {
        for (int i = 0; i < a->nalts; i++) {
            int save[2 * RX_MAXCAP];
            RxGroupCont g = { a, pos, cont, ctx };
            rx_save(st, save);
            if (rx_m_seq(&a->alts[i], 0, st, pos, rx_group_cont, &g)) return 1;
            rx_restore(st, save);
        }
        return 0;
    }
    }
    return 0;
}

typedef struct {
    size_t end;
    int matched;
} RxTopCont;

static int rx_top_cont(RxState *st, size_t pos, void *vctx)
{
    (void)st;
    RxTopCont *t = vctx;
    t->end = pos;
    t->matched = 1;
    return 1;
}

/* try to match the whole pattern at `start'; returns 1 and fills *end on
 * success */
static int rx_match_at(const RxSeq *seq, RxState *st, size_t start, size_t *end)
{
    RxTopCont t = { 0, 0 };
    if (!rx_m_seq(seq, 0, st, start, rx_top_cont, &t)) return 0;
    *end = t.end;
    return 1;
}

static void rx_free_seq(RxSeq *sq);

static void rx_free_atom(RxAtom *a)
{
    if (!a) return;
    for (int i = 0; i < a->nalts; i++) rx_free_seq(&a->alts[i]);
    free(a->alts);
    free(a->cls);
    free(a);
}

static void rx_free_seq(RxSeq *sq)
{
    for (int i = 0; i < sq->n; i++) rx_free_atom(sq->items[i].atom);
    free(sq->items);
    sq->items = NULL;
    sq->n = 0;
}

/* ----------------------------------------------------------- substitution */

static void rx_expand(const char *rep, size_t replen, const RxState *st,
                      size_t mstart, size_t mend, SB *out)
{
    for (size_t i = 0; i < replen; i++) {
        char c = rep[i];
        if (c == '$' && i + 1 < replen) {
            char d = rep[i + 1];
            if (d == '&') { sb_putn(out, st->s + mstart, mend - mstart); i++; continue; }
            if (d >= '1' && d <= '9') {
                int k = d - '0';
                if (st->capstart[k] >= 0 && st->capend[k] >= st->capstart[k])
                    sb_putn(out, st->s + st->capstart[k], (size_t)(st->capend[k] - st->capstart[k]));
                i++;
                continue;
            }
            sb_putc(out, '$');
            continue;
        }
        if (c == '\\' && i + 1 < replen) {
            char d = rep[++i];
            switch (d) {
            case 'n': sb_putc(out, '\n'); break;
            case 't': sb_putc(out, '\t'); break;
            case 'r': sb_putc(out, '\r'); break;
            case 'f': sb_putc(out, '\f'); break;
            case 'a': sb_putc(out, '\a'); break;
            case 'e': sb_putc(out, '\033'); break;
            default:  sb_putc(out, d); break;
            }
            continue;
        }
        sb_putc(out, c);
    }
}

/* apply one compiled pattern to a string; returns 0 when nothing matched and
 * the output then has to be thrown away by the caller */
static int rx_apply(const RxSeq *seq, const char *subject, const char *rep,
                    size_t replen, int global, SB *out)
{
    RxState st;
    memset(&st, 0, sizeof st);
    st.s = subject;
    st.len = strlen(subject);
    for (int i = 0; i < RX_MAXCAP; i++) { st.capstart[i] = -1; st.capend[i] = -1; }

    size_t pos = 0, len = st.len;
    int done = 0;
    while (pos <= len) {
        size_t found = (size_t)-1, foundEnd = 0;
        for (size_t start = pos; start <= len; start++) {
            int save[2 * RX_MAXCAP];
            rx_save(&st, save);
            if (rx_match_at(seq, &st, start, &foundEnd)) { found = start; break; }
            rx_restore(&st, save);
        }
        if (found == (size_t)-1) break;
        sb_putn(out, subject + pos, found - pos);
        rx_expand(rep, replen, &st, found, foundEnd, out);
        done = 1;
        pos = foundEnd;
        if (!global) break;
        if (foundEnd == found) {
            /* an empty match: Perl keeps the character and moves on */
            if (foundEnd < len) sb_putc(out, subject[foundEnd]);
            pos = foundEnd + 1;
        }
    }
    if (pos <= len) sb_putn(out, subject + pos, len - pos);
    return done;
}

/* read one section of a substitution; `*pp` points at the first character
 * after the opening delimiter (or at the section itself for the unpaired
 * form) and is left after the closing delimiter */
static int rx_read_section(const char **pp, char delim, char closeDelim,
                           SB *dst, char *err, size_t errsz)
{
    const char *p = *pp;
    int depth = closeDelim ? 1 : 0;
    while (*p) {
        if (*p == '\\' && p[1]) { sb_putc(dst, *p); sb_putc(dst, p[1]); p += 2; continue; }
        if (closeDelim) {
            if (*p == delim) depth++;
            else if (*p == closeDelim) {
                depth--;
                if (depth == 0) { p++; *pp = p; return 1; }
            }
        } else if (*p == delim) {
            p++;
            *pp = p;
            return 1;
        }
        sb_putc(dst, *p++);
    }
    *pp = p;
    snprintf(err, errsz, "unterminated substitution");
    return 0;
}

/* the closing delimiter that belongs to an opening one (perlop's "bracketing
 * quotes"), 0 when the delimiter stands alone */
static char rx_matching(char delim)
{
    switch (delim) {
    case '{': return '}';
    case '[': return ']';
    case '(': return ')';
    case '<': return '>';
    default:  return 0;
    }
}

/* parse `s{pat}{rep}mods' and return 1 when the command was understood */
static int rx_parse_subst(const char *cmd, SB *pat, SB *rep, int *global,
                          char *err, size_t errsz)
{
    const char *p = cmd;
    *global = 0;
    if (*p != 's') { snprintf(err, errsz, "not a substitution"); return 0; }
    p++;
    if (!*p) { snprintf(err, errsz, "missing delimiter"); return 0; }
    char delim = *p++;
    char closeDelim = rx_matching(delim);
    if (!rx_read_section(&p, delim, closeDelim, pat, err, errsz)) return 0;
    if (closeDelim) {
        /* "If the PATTERN is delimited by bracketing quotes, the REPLACEMENT
         * has its own pair of quotes" -- perlop */
        if (!*p) { snprintf(err, errsz, "missing replacement delimiter"); return 0; }
        char rdelim = *p++;
        char rclose = rx_matching(rdelim);
        if (!rx_read_section(&p, rdelim, rclose, rep, err, errsz)) return 0;
    } else if (!rx_read_section(&p, delim, 0, rep, err, errsz)) {
        return 0;
    }
    for (; *p; p++) {
        if (*p == 'g') { *global = 1; continue; }
        if (is_space_perl(*p)) break;
        snprintf(err, errsz, "unsupported modifier '%c'", *p);
        return 0;
    }
    while (is_space_perl(*p)) p++;
    if (*p) { snprintf(err, errsz, "trailing text after the modifiers"); return 0; }
    return 1;
}

/* ------------------------------------------------------------ esd commands */

typedef struct {
    char *key;
    char *cmd;
    int warned;
} EsdCommand;

static EsdCommand *EsdCommands = NULL;
static int nEsdCommands = 0;
static int EsdLoaded = 0;              /* set by `{#use esd}' */

static int esd_keyorder(const void *A, const void *B)
{
    return strcmp(((const EsdCommand *)A)->key, ((const EsdCommand *)B)->key);
}

static char *esd_apply_commands(const char *line)
{
    char *cur = xstrdup(line);
    for (int i = 0; i < nEsdCommands; i++) {
        if (EsdCommands[i].cmd[0] == '\0') continue;   /* eval undef: no-op */
        SB pat, rep, out;
        sb_init(&pat); sb_init(&rep); sb_init(&out);
        int global = 0;
        char err[160];
        err[0] = '\0';
        if (!rx_parse_subst(EsdCommands[i].cmd, &pat, &rep, &global, err, sizeof err)) {
            sb_free(&out);
        } else {
            RxSeq seq;
            int ncap = 0;
            if (!rx_compile(pat.p, &seq, &ncap, err, sizeof err)) {
                sb_free(&out);
            } else {
                int done = rx_apply(&seq, cur, rep.p, strlen(rep.p), global, &out);
                rx_free_seq(&seq);
                if (done) { free(cur); cur = sb_detach(&out); }
                else sb_free(&out);
            }
        }
        sb_free(&pat);
        sb_free(&rep);
        if (err[0] && !EsdCommands[i].warned) {
            EsdCommands[i].warned = 1;
            if (!SupressWarnings) {
                SB m; sb_init(&m);
                sb_puts(&m, "esd: the command <");
                sb_puts(&m, EsdCommands[i].cmd);
                sb_puts(&m, "> is not applied: ");
                sb_puts(&m, err);
                sb_puts(&m, "\n");
                fputs(m.p, stderr);
                sb_free(&m);
            }
        }
    }
    return cur;
}

/* {#esd::command <key> <perl-substitution>} -- esd.pm:
 *     $arg =~ /^\s+(\w+)\s+(.*)/;  $COMMAND{$name} = $command; */
static char *esd_command(const char *arg)
{
    char *key = NULL, *cmd = NULL;
    if (is_space_perl(*arg)) {
        const char *p = arg;
        while (is_space_perl(*p)) p++;
        const char *ks = p;
        while (p_isword(*p)) p++;
        if (p != ks && is_space_perl(*p)) {
            const char *ke = p;
            while (is_space_perl(*p)) p++;
            const char *cs = p;
            while (*p && *p != '\n') p++;
            key = xmemdup(ks, (size_t)(ke - ks));
            cmd = xmemdup(cs, (size_t)(p - cs));
        }
    }
    if (!key) {
        /* no match: $1 and $2 stay undef, which registers the empty key with
         * an undef command -- a command that does nothing */
        key = xstrdup("");
        cmd = xstrdup("");
    }
    for (int i = 0; i < nEsdCommands; i++) {
        if (strcmp(EsdCommands[i].key, key) == 0) {
            free(EsdCommands[i].cmd);
            EsdCommands[i].cmd = cmd;
            EsdCommands[i].warned = 0;
            free(key);
            return xstrdup("");
        }
    }
    EsdCommands = xrealloc(EsdCommands, (size_t)(nEsdCommands + 1) * sizeof *EsdCommands);
    EsdCommands[nEsdCommands].key = key;
    EsdCommands[nEsdCommands].cmd = cmd;
    EsdCommands[nEsdCommands].warned = 0;
    nEsdCommands++;
    /* esd.pm uses the Perl hash order here, which is not reproducible; the
     * files are written for the numeric order of their keys */
    qsort(EsdCommands, nEsdCommands, sizeof *EsdCommands, esd_keyorder);
    return xstrdup("");
}

/* {#esd::nocommand} */
static char *esd_nocommand(void)
{
    for (int i = 0; i < nEsdCommands; i++) {
        free(EsdCommands[i].key);
        free(EsdCommands[i].cmd);
    }
    free(EsdCommands);
    EsdCommands = NULL;
    nEsdCommands = 0;
    return xstrdup("");
}

/* {#esd::include <file> [<start> [<stop>]]} -- esd.pm reads the file, runs
 * every registered command on every line and keeps the lines between the
 * start and the stop markers. */
static char *esd_include(const char *arg, const char *body)
{
    char *field[3] = {NULL, NULL, NULL};
    int nf = 0;
    const char *p = arg;
    while (is_space_perl(*p)) p++;
    while (*p && nf < 3) {
        const char *s = p;
        while (*p && !is_space_perl(*p)) p++;
        field[nf++] = xmemdup(s, (size_t)(p - s));
        while (is_space_perl(*p)) p++;
    }
    const char *start = field[1] ? field[1] : "";
    const char *stop  = field[2] ? field[2] : "";

    int ok = 0;
    char *content = read_whole_file(field[0] ? field[0] : "", &ok);
    if (!ok) {
        char msg[512];
        snprintf(msg, sizeof msg, "Can not open %s", field[0] ? field[0] : "");
        for (int i = 0; i < nf; i++) free(field[i]);
        return Error(msg, body);
    }

    SB out;
    sb_init(&out);
    int sw = 0;
    const char *q = content;
    while (*q) {
        const char *nl = strchr(q, '\n');
        size_t seglen = nl ? (size_t)(nl - q) : strlen(q);
        char *line = xmemdup(q, seglen);          /* chomp */
        q += seglen + (nl ? 1 : 0);
        /* esd.pm reads the file in text mode: a CRLF source must lose the CR,
         * otherwise the start and stop markers below never match. */
        if (seglen > 0 && line[seglen - 1] == '\r') line[seglen - 1] = '\0';
        if (sw == 0 && strcmp(line, start) == 0) { sw = 1; free(line); continue; }
        if (sw == 1 && strcmp(line, stop) == 0)  { sw = 0; free(line); continue; }
        char *sub = esd_apply_commands(line);
        if (sw) { sb_puts(&out, sub); sb_putc(&out, '\n'); }
        free(sub);
        free(line);
    }
    free(content);
    for (int i = 0; i < nf; i++) free(field[i]);
    return sb_detach(&out);
}

/* {#esd::execute <command>} -- esd.pm: $arg =~ s{/}{\\}g; return `$arg`; */
static char *esd_execute(const char *arg)
{
    SB cmd;
    sb_init(&cmd);
    for (const char *p = arg; *p; p++) sb_putc(&cmd, *p == '/' ? '\\' : *p);
    SB out;
    sb_init(&out);
    FILE *f = popen(cmd.p, "r");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_putn(&out, buf, n);
        pclose(f);
    }
    sb_free(&cmd);
    return sb_detach(&out);
}

/* dispatch `{#<pkg>::<macro> <rest>}'.  Returns the text the macro produced;
 * `*failed' is set when the call itself is an error (the text is then the
 * error report, which the caller returns as is, like the original does). */
static char *esd_call(const char *pkg, const char *macro, const char *arg,
                      const char *body, int *failed)
{
    *failed = 0;
    if (EsdLoaded && strcmp(pkg, "esd") == 0) {
        if (!strcmp(macro, "::command"))   return esd_command(arg);
        if (!strcmp(macro, "::nocommand")) return esd_nocommand();
        if (!strcmp(macro, "::include"))   return esd_include(arg, body);
        if (!strcmp(macro, "::execute"))   return esd_execute(arg);
    }
    char msg[768];
    if (strcmp(pkg, "esd") == 0)
        snprintf(msg, sizeof msg, "Extension macro resulted the error: Undefined "
                 "subroutine &jamal::esd%s called", macro);
    else
        snprintf(msg, sizeof msg, "Extension macro resulted the error: only the "
                 "esd extension module is built into this jamal, not jamal::%s",
                 pkg);
    *failed = 1;
    return Error(msg, body);
}
/* The macro evaluator.  `input` is the text between the delimiters: the leading
 * `#`, `@` or `?` is still part of it, exactly as in the original. */
static char *EvalMacro(const char *input, const char *refFile, int level)
{
    int verbatim = 0;
    int ReportNotDefinedMacro = 1;

    char *in = xstrdup(input);

    /* $verbatim = 1 if $$input =~ s/^(?:#|@)verbatim\s+// ; */
    if ((in[0] == '#' || in[0] == '@') && starts_with(in + 1, "verbatim")) {
        const char *p = in + 9;
        const char *q = p;
        while (is_space_perl(*q)) q++;
        if (q > p) { verbatim = 1; memmove(in, q, strlen(q) + 1); }
    }

    /* The skipping loops of the original compare the bareword `char` against
     * "\t" and "\r", which is never true, so only space and newline are
     * skipped.  Reproduced as is. */
    const char *s = in;
    while (*s == ' ' || *s == '\n') s++;

    int builtin = 0;
    if (*s == '@' || *s == '#') {
        s++;
        while (*s == ' ' || *s == '\n') s++;
        builtin = 1;
    } else {
        if (*s == '?') {
            s++;
            while (*s == ' ' || *s == '\n') s++;
            ReportNotDefinedMacro = 0;
        }
    }

    char *body = xstrdup(s);              /* $$input = substr($$input,$index) */
    free(in);

    if (builtin) {
        const char *rest = NULL;

        /* Perl module extension macro `{base::Macro rest}`.  The original
         * evals `&jamal::<base>::<macro>(<rest>)' so that the extension package
         * does the parsing; this port implements the one extension module the
         * tree uses, esd, and reports the others the way a module that cannot
         * be loaded is reported. */
        {
            const char *p = body;
            while (p_isword(*p)) p++;
            if (p[0] == ':' && p[1] == ':') {
                const char *baseEnd = p;
                const char *q = p;
                while (q[0] == ':' && q[1] == ':' && p_isword(q[2])) {
                    q += 2;
                    while (p_isword(*q)) q++;
                }
                char *base  = xmemdup(body, (size_t)(baseEnd - body));
                char *macro = xmemdup(p, (size_t)(q - p));
                const char *pkg = *base ? base : (BaseModule ? BaseModule : "");
                const char *arg = q;   /* the rest, leading spaces included */
                int failed = 0;
                char *result = esd_call(pkg, macro, arg, body, &failed);
                char *out = failed ? result : DoInput(result, refFile, level + 1);
                if (!failed) free(result);
                free(base);
                free(macro);
                free(body);
                return out;
            }
        }

        if (after_prefix_nonword(body, "require", &rest)) {
            Vec *l = Split(rest, 1);
            const char *rv = vec_get(l, 0);
            if (rv && p_num(rv) > p_num(JAMAL_VERSION)) {
                char msg[256];
                snprintf(msg, sizeof msg,
                         "This file requires jamal V%s and I am only %s", rv, JAMAL_VERSION);
                fatal(msg, body);
            }
            vec_free(l);
            free(body);
            return xstrdup("");
        }

        if (after_prefix_nonword(body, "with", &rest)) {
            Vec *l = Split(rest, 1);
            /* The original stores `$$package` where $package holds an array
             * reference, which aborts at run time for every reachable input,
             * so no faithful behaviour exists to copy.  The natural reading --
             * push the current base module, adopt the new one -- is what this
             * port implements. */
            const char *pkg = vec_get(l, 0);
            if (pkg) {
                vec_push(BaseModules, xstrdup(BaseModule ? BaseModule : ""));
                free(BaseModule);
                BaseModule = xstrdup(pkg);
            } else if (BaseModules->n > 0) {
                const char *prev = BaseModules->v[BaseModules->n - 1];
                free(BaseModule);
                BaseModule = xstrdup(prev);
                free(BaseModules->v[--BaseModules->n]);
            }
            vec_free(l);
            free(body);
            return xstrdup("");
        }

        if (after_prefix_nonword(body, "use", &rest)) {
            Vec *l = Split(rest, 2);
            const char *package = vec_get(l, 0);
            const char *version = vec_get(l, 1);
            if (package && strcmp(package, "esd") == 0) {
                /* esd.pm ships with the tree and is built into this port */
                if (EsdLoaded) Warning("Extension esd is already loaded.", body);
                EsdLoaded = 1;
                /* esd.pm calls jamal::version('1.0','2.0') */
                if (version && *version && (p_num(version) > 2.0 || p_num(version) < 1.0)) {
                    char msg[256];
                    snprintf(msg, sizeof msg,
                             "Extension esd does not implement the requested version %s.",
                             version);
                    fatal(msg, body);
                }
                vec_push(BaseModules, xstrdup(BaseModule ? BaseModule : ""));
                free(BaseModule);
                BaseModule = xstrdup("esd");
                vec_free(l);
                free(body);
                return xstrdup("");
            }
            char msg[512];
            snprintf(msg, sizeof msg,
                     "Extension %s is not found. Error message: this port cannot "
                     "load Perl extension modules.", package ? package : "");
            char *r = Error(msg, body);
            vec_free(l);
            free(body);
            return r;
        }

        if (after_prefix_nonword(body, "sep", &rest)) {
            Vec *l = Split(rest, 2);
            const char *mO = vec_get(l, 0);
            const char *mC = vec_get(l, 1);
            if (!mO || !*mO) {
                char *r = Error("Semantic error in macro SEP would result empty macro open string", body);
                vec_free(l); free(body);
                return r;
            }
            if (!mC || !*mC) {
                char *r = Error("Semantic error in macro SEP would result empty macro close string", body);
                vec_free(l); free(body);
                return r;
            }
            free(macroOpen);  macroOpen  = xstrdup(mO);
            free(macroClose); macroClose = xstrdup(mC);
            mopenCNL  = cnl(macroOpen);
            mcloseCNL = cnl(macroClose);
            vec_free(l);
            free(body);
            return xstrdup("");
        }

        /* {#for i/1 ... 10/ body} and {#for i/,a,b,c/ body} */
        if (strncmp(body, "for", 3) == 0 && is_space_perl(body[3])) {
            const char *p = body + 3;
            while (is_space_perl(*p)) p++;
            const char *nameA = p;
            while (p_isword(*p)) p++;
            const char *nameB = p;
            while (is_space_perl(*p)) p++;
            if (nameB > nameA && *p) {
                char *loopVar = xmemdup(nameA, (size_t)(nameB - nameA));
                const char *loop = p + 1;                    /* s/^(.|\n)// */
                char closeCh = char_pair_char(p[0]);
                const char *end = strchr(loop, closeCh);
                char *loopP = end ? xmemdup(loop, (size_t)(end - loop)) : xstrdup(loop);
                const char *loopBody = end ? end + 1 : "";

                SB out; sb_init(&out);
                char *from = NULL, *to = NULL;
                if (numeric_range(loopP, &from, &to)) {
                    long long a = strtoll(from, NULL, 10);
                    long long b = strtoll(to, NULL, 10);
                    char cur[64];
                    snprintf(cur, sizeof cur, "%s", from);
                    while (1) {
                        char *piece = str_replace_all(loopBody, loopVar, cur);
                        sb_puts(&out, piece);
                        free(piece);
                        if (a == b) break;
                        a += (a < b) ? 1 : -1;
                        snprintf(cur, sizeof cur, "%lld", a);
                    }
                } else {
                    Vec *argv = Split(loopP, INT_MIN);
                    for (int i = 0; i < argv->n; i++) {
                        char *piece = str_replace_all(loopBody, loopVar, argv->v[i]);
                        sb_puts(&out, piece);
                        free(piece);
                    }
                    vec_free(argv);
                }
                free(from);
                free(to);
                char *res = DoInput(out.p, refFile, level + 1);
                sb_free(&out);
                free(loopVar);
                free(loopP);
                free(body);
                return res;
            }
        }

        /* {#dir ...} walks a directory tree; it is not used anywhere in this
         * repository and is not ported. */
        if (after_prefix_nonword(body, "dir", &rest)) {
            char *r = Error("The {#dir} macro is not implemented by this port.", body);
            free(body);
            return r;
        }

        if (after_prefix_nonword(body, "months", &rest)) {
            Vec *l = Split(rest, 12);
            vec_free(MonthNames);
            MonthNames = l;
            if (l->n < 12) {
                char *r = Error("There are not enough month names defined:", body);
                free(body);
                return r;
            }
            if (l->n > 12) {
                char *r = Error("There are too many month names defined:", body);
                free(body);
                return r;
            }
            free(body);
            return xstrdup("");
        }

        if (after_prefix_nonword(body, "days", &rest)) {
            Vec *l = Split(rest, 7);
            vec_free(DayNames);
            DayNames = l;
            if (l->n < 7) {
                char *r = Error("There are not enough day names defined:", body);
                free(body);
                return r;
            }
            if (l->n > 7) {
                char *r = Error("There are too many day names defined:", body);
                free(body);
                return r;
            }
            free(body);
            return xstrdup("");
        }

        {
            const char *which = NULL, *value = NULL;
            if (match_format(body, &which, &value)) {
                if (strcmp(which, "time") == 0) { free(TimeFormat); TimeFormat = xstrdup(value); }
                else if (strcmp(which, "date") == 0) { free(DateFormat); DateFormat = xstrdup(value); }
                else if (strcmp(which, "am") == 0) { free(AM); AM = xstrdup(value); }
                else { free(PM); PM = xstrdup(value); }
                free(body);
                return xstrdup("");
            }
        }

        if (match_bare_word(body, "time")) {
            SB b; sb_init(&b);
            convertTimeDate(TimeFormat, timeNow, &b);
            char *r = DoInput(b.p, refFile, level + 1);
            sb_free(&b);
            free(body);
            return r;
        }

        if (match_bare_word(body, "date")) {
            SB b; sb_init(&b);
            convertTimeDate(DateFormat, timeNow, &b);
            char *r = DoInput(b.p, refFile, level + 1);
            sb_free(&b);
            free(body);
            return r;
        }

        /* {#select/n/,1,2,3,4,5} */
        if (after_prefix_nonword(body, "select", &rest)) {
            Vec *l = Split(rest, 2);
            const char *n = vec_get(l, 0);
            const char *list = vec_get(l, 1);
            Vec *items = Split(list ? list : "", INT_MIN);
            long idx = n ? strtol(n, NULL, 10) : 0;
            if (idx < 0) idx += items->n;
            const char *sel = (idx >= 0 && idx < items->n) ? items->v[idx] : NULL;
            char *r = DoInput(sel ? sel : "", refFile, level + 1);
            vec_free(items);
            vec_free(l);
            free(body);
            return r;
        }

        /* {#if/test/then/else} */
        if (after_prefix_nonword(body, "if", &rest)) {
            Vec *l = Split(rest, 3);
            const char *test = vec_get(l, 0);
            const char *then = vec_get(l, 1), *els = vec_get(l, 2);
            char *r = DoInput(p_true(test) ? (then ? then : "") : (els ? els : ""),
                              refFile, level + 1);
            vec_free(l);
            free(body);
            return r;
        }

        {   /* {#null text} */
            const char *cap = NULL;
            if (match_swallow(body, "null", &cap)) {
                char *r = xstrdup(cap);
                free(body);
                return r;
            }
            /* {#comment text} */
            if (match_swallow(body, "comment", &cap)) {
                free(body);
                return xstrdup("");
            }
        }

        /* {#op /exp1/exp2/.../expn} */
        {
            size_t len = strlen(body);
            char f3[4] = {0}, f2[3] = {0}, f1[2] = {0};
            for (size_t i = 0; i < 3 && i < len; i++) f3[i] = body[i];
            for (size_t i = 0; i < 2 && i < len; i++) f2[i] = body[i];
            if (len > 0) f1[0] = body[0];

            const char *op = NULL;
            size_t skip = 0;
            if (strcmp(f3, "and") == 0 || strcmp(f3, "not") == 0) {
                op = f3; skip = 3;
            } else if (strcmp(f2, "<=") == 0 || strcmp(f2, ">=") == 0 ||
                       strcmp(f2, "!=") == 0 || strcmp(f2, "eq") == 0 ||
                       strcmp(f2, "ne") == 0 || strcmp(f2, "gt") == 0 ||
                       strcmp(f2, "lt") == 0 || strcmp(f2, "ge") == 0 ||
                       strcmp(f2, "le") == 0 || strcmp(f2, "or") == 0 ||
                       (f2[0] == '-' && strchr("zsdfet", tolower((unsigned char)f2[1])))) {
                op = f2; skip = 2;
            } else if (len == 0 || strchr("=<>*/+-", f1[0])) {
                op = f1; skip = 1;
            }

            if (op) {
                int matched = 0;
                char *r = eval_operators(op, body + (skip <= len ? skip : len),
                                         refFile, level, &matched);
                if (matched) {
                    free(body);
                    return r;
                }
                free(r);
            }
        }

        {   /* {#define ...} */
            DefineParse d = parse_define(body);
            if (d.ok) {
                SB nm; sb_init(&nm);
                if (d.baseLen) sb_putn(&nm, d.base, d.baseLen);
                else if (BaseModule) sb_puts(&nm, BaseModule);
                sb_putn(&nm, d.module, d.moduleLen);
                char *name = sb_detach(&nm);
                char *param = xmemdup(d.param, d.paramLen);
                if (!(d.conditional && macro_value(name)))
                    DefineMacro(name, param, d.value);
                free(name);
                free(param);
                free(body);
                return xstrdup("");
            }
        }

        {   /* {#undef name} */
            char *name = NULL;
            if (match_undef(body, &name)) {
                macro_delete(name);
                free(name);
                free(body);
                return xstrdup("");
            }
        }

        {   /* {#INC directory} */
            char *dir = NULL;
            if (match_inc(body, &dir)) {
                char *abs = Relative(refFile, dir);
                vec_push(IncludeDirs, abs);
                free(dir);
                free(body);
                return xstrdup("");
            }
        }

        {   /* {#restore name} */
            char *name = NULL;
            if (match_restore(body, &name)) {
                char *value = NULL;
                Vec *args = NULL;
                if (stack_pop_saved(name, &value, &args)) macro_set(name, value, args);
                else macro_delete(name);
                free(name);
                free(body);
                return xstrdup("");
            }
        }

        /* The include family, in the order the original tests them. */
        if (strncmp(body, "include", 7) == 0) {
            const char *p = body + 7;
            const char *q = p;
            while (is_space_perl(*q)) q++;
            if (q != p) {
                char *name = NULL;
                if (match_ws_name(q, "pre", &name)) {
                    char *r = include_pre(refFile, name);
                    free(name); free(body);
                    return r;
                }
                if (match_ws_name(q, "verbatim", &name)) {
                    char *r = include_verbatim(refFile, name);
                    free(name); free(body);
                    return r;
                }
                if (match_ws_name(q, "macro", &name) || match_ws_name(q, "macros", &name)) {
                    char *file = Relative(refFile, name);
                    char *r = DoFile(file);
                    free(r);
                    free(file); free(name); free(body);
                    return xstrdup("");
                }
                char *file = Relative(refFile, strip_optional_quotes(q));
                char *r = DoFile(file);
                free(file);
                free(body);
                return r;
            }
        }

        if (match_single_char(body, "[{")) { char *r = xstrdup(macroOpen);  free(body); return r; }
        if (match_single_char(body, "]}")) { char *r = xstrdup(macroClose); free(body); return r; }

        {
            char *r = Error("Bad macro.", body);
            free(body);
            return r;
        }
    }

    /* {$File$name} style loop variables -- only meaningful inside the loop of
     * the {#dir} macro. */
    if (is_loop_macro(body)) {
        const char *v = WeAreInLoop ? macro_value(body) : NULL;
        if (WeAreInLoop) {
            char *r = xstrdup(v ? v : "");
            free(body);
            return r;
        }
        SB b; sb_init(&b);
        sb_puts(&b, macroOpen);
        sb_puts(&b, body);
        sb_puts(&b, macroClose);
        free(body);
        return sb_detach(&b);
    }

    /* a user defined macro */
    {
        const char *p = body;
        while (p_isword(*p)) p++;
        if (p == body) {
            SB b; sb_init(&b);
            sb_puts(&b, macroOpen);
            sb_puts(&b, body);
            sb_puts(&b, macroClose);
            char *r = Error("Bad format macro:", b.p);
            sb_free(&b);
            free(body);
            return r;
        }
        while (p[0] == ':' && p[1] == ':') {
            const char *r = p + 2;
            if (!p_isword(*r)) break;
            while (p_isword(*r)) r++;
            p = r;
        }

        char *name = xmemdup(body, (size_t)(p - body));
        const char *args = p;
        MacroEntry *e = macro_find(name);
        int argn = (e && e->args) ? e->args->n - 1 : -1;

        if (argn == -1) {
            const char *a = args;
            while (is_space_perl(*a)) a++;
            if (*a) {
                SB b; sb_init(&b);
                sb_puts(&b, macroOpen);
                sb_puts(&b, body);
                sb_puts(&b, macroClose);
                Warning("Macro needs no argument:", b.p);
                sb_free(&b);
            }
        }

        Vec *argv = (argn > -1) ? Split(args, argn + 1) : vec_new();
        const char *value = e ? e->value : NULL;

        if (!value) {
            if (ReportNotDefinedMacro) {
                char *r = Error("Macro is not defined!", name);
                vec_free(argv); free(name); free(body);
                return r;
            }
            vec_free(argv); free(name); free(body);
            return xstrdup("");
        }

        char *expanded = (e && e->args) ? expand_formals(value, e->args, argv)
                                        : xstrdup(value);
        vec_free(argv);
        free(name);
        free(body);

        if (verbatim) return expanded;
        char *r = DoInput(expanded, refFile, level + 1);
        free(expanded);
        return r;
    }
}

/* ------------------------------------------------------------------- main */

static void help(void)
{
    printf("jamal html preprocessor V%s\n", JAMAL_VERSION);
    printf("Usage:\n.\njamal [-options] input [output]\n.\n");
    printf("Options:\n");
    printf("-? or -h               help\n");
    printf("-m file                macro definition file\n");
    printf("-D                     macro define\n");
    printf("-I                     j-SEX directory\n");
    printf("-i                     install j-SEX module interactive\n");
    printf("-0                     suppress output\n");
    printf("-d                     put error message into result\n");
    printf("-t file                trace file\n");
    printf(".\nYour default extension directories are:\n");
    for (int i = 0; IncludeDirs && i < IncludeDirs->n; i++)
        printf("%s\n", IncludeDirs->v[i]);
}

int main(int argc, char **argv)
{
    timeNow = time(NULL);

    macroOpen = xstrdup("{");
    macroClose = xstrdup("}");
    mopenCNL = cnl(macroOpen);
    mcloseCNL = cnl(macroClose);
    Macrofiles = vec_new();
    IncludeDirs = vec_new();
    BaseModules = vec_new();
    FileStack = vec_new();

    int i = 1;
    while (i < argc) {
        char *arg = argv[i++];
        if (arg[0] == '-' && arg[1] != '\0') {
            const char *o = arg + 1;
            if (strcmp(o, "?") == 0 || strcmp(o, "h") == 0) { help(); return 0; }
            if (strcmp(o, "m") == 0) {
                if (i < argc) vec_push(Macrofiles, xstrdup(argv[i++]));
                continue;
            }
            if (strcmp(o, "I") == 0) {
                if (i < argc) vec_push(IncludeDirs, xstrdup(argv[i++]));
                continue;
            }
            if (strcmp(o, "0") == 0) { SuppressOutput = 1; continue; }
            if (strcmp(o, "d") == 0) { OutputError = 1; continue; }
            if (strcmp(o, "t") == 0) {
                if (i < argc) TraceFile = argv[i++];
                continue;
            }
            if (strcmp(o, "i") == 0) {
                fputs("The interactive j-SEX installer is not available in this port.\n", stderr);
                return 1;
            }
            /* -Dname=value : /\D(\w+)((?:.|\n)*?)=((?:.|\n)*)$/ on the
             * option text (leading '-' already removed). */
            {
                const char *s0 = NULL;
                for (const char *t = o; *t; t++)
                    if (!isdigit((unsigned char)*t)) { s0 = t; break; }
                if (s0) {
                    const char *w = s0 + 1;
                    while (p_isword(*w)) w++;
                    const char *eq = strchr(w, '=');
                    if (w > s0 + 1 && eq) {
                        char *nm  = xmemdup(s0 + 1, (size_t)(w - (s0 + 1)));
                        char *mid = xmemdup(w, (size_t)(eq - w));
                        DefineMacro(nm, mid, eq + 1);
                        free(nm); free(mid);
                        continue;
                    }
                }
            }
            {
                /* the original shifts the leading '-' off $_ before it
                 * reports the option */
                char *e = Error("Invalid option. Ignored.", arg[0] == '-' ? arg + 1 : arg);
                free(e);
            }
        } else {
            if (!Source) { Source = xstrdup(arg); continue; }
            if (!Destination) { Destination = xstrdup(arg); continue; }
            fputs("There are too many command line arguments\njamal -?", stderr);
            return 1;
        }
    }

    if (!Source) {
        char *e = Error("No input is defined.", NULL);
        free(e);
        return 1;
    }

    if (TraceFile && maked(TraceFile)) {
        TraceFP = fopen(TraceFile, "w");
        if (!TraceFP) {
            char *e = Error("Trace file can not be opened.", TraceFile);
            free(e);
            TraceFile = NULL;
        }
    }

    /* make file names UNIX compliant under Windows NT */
    if (Source) for (char *p = Source; *p; p++) if (*p == '\\') *p = '/';
    if (Destination) for (char *p = Destination; *p; p++) if (*p == '\\') *p = '/';

    PredefineMacro();

    for (int k = 0; k < Macrofiles->n; k++) {
        char *r = DoFile(Macrofiles->v[k]);
        free(r);
    }

    FILE *out = stdout;
    if (Destination && maked(Destination)) {
        out = fopen(Destination, "wb");
        if (!out) {
            char *e = Error("Output file can not be opened.", Destination);
            free(e);
            return 1;
        }
    }

    char *result = DoFile(Source);
    if (Destination) {
        fputs(result, out);
        fclose(out);
    } else if (!SuppressOutput) {
        fputs(result, out);
    }
    free(result);

    if (TraceFP) fclose(TraceFP);
    return 0;
}
