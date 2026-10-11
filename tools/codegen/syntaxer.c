/*
 * syntaxer.c -- C23 replacement for syntaxer.pl
 *
 * Converts syntax.def into the C language tables used by lexer.c and
 * expression.c: syntax.c (whose header syntax.h and notimp.h are later
 * extracted by headerer.c through the TO_HEADER markers) plus notimp.c.
 *
 * Difference from the Perl original: it iterated %COMMANDS and %SYMBOL,
 * two Perl hashes whose order is randomised per process, so two runs of
 * the original produced different (internally consistent) files and the
 * build was not reproducible. This port iterates the symbols in the
 * order they first appear in syntax.def, which makes the output stable.
 * Every other behaviour, including the byte layout, matches the original.
 *
 * Usage: syntaxer [syntax.def]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* -------------------------------------------------------------------- */
/* small helpers                                                         */
/* -------------------------------------------------------------------- */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("out of memory\n", stderr); exit(1); }
    return q;
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

static int sp(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int wd(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/* -------------------------------------------------------------------- */
/* dynamic data structures                                               */
/* -------------------------------------------------------------------- */

typedef struct { char *buf; size_t len, cap; } Buf;

static void buf_init(Buf *b)
{
    b->cap = 256;
    b->len = 0;
    b->buf = xmalloc(b->cap);
    b->buf[0] = '\0';
}

static void buf_room(Buf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->buf = xrealloc(b->buf, b->cap);
    }
}

static void buf_add(Buf *b, const char *data, size_t n)
{
    if (!n) return;
    buf_room(b, n);
    memcpy(b->buf + b->len, data, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
static void buf_ch(Buf *b, char c) { buf_add(b, &c, 1); }

static void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_room(b, (size_t)n);
        vsnprintf(b->buf + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

/* insertion ordered string -> long map (stands for the Perl hashes) */
typedef struct { char *name; long value; } Entry;
typedef struct { Entry *e; size_t n, cap; } Map;

static void map_set(Map *m, const char *name, long value)
{
    for (size_t k = 0; k < m->n; k++) {
        if (strcmp(m->e[k].name, name) == 0) { m->e[k].value = value; return; }
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 64;
        m->e = xrealloc(m->e, m->cap * sizeof(Entry));
    }
    m->e[m->n].name = xstrdup(name);
    m->e[m->n].value = value;
    m->n++;
}

static int map_get(const Map *m, const char *name, long *out)
{
    for (size_t k = 0; k < m->n; k++) {
        if (strcmp(m->e[k].name, name) == 0) { *out = m->e[k].value; return 1; }
    }
    return 0;
}

typedef struct { char *name; char *value; } SEntry;
typedef struct { SEntry *e; size_t n, cap; } SMap;

static void smap_set(SMap *m, const char *name, const char *value)
{
    for (size_t k = 0; k < m->n; k++) {
        if (strcmp(m->e[k].name, name) == 0) {
            free(m->e[k].value);
            m->e[k].value = xstrdup(value);
            return;
        }
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 64;
        m->e = xrealloc(m->e, m->cap * sizeof(SEntry));
    }
    m->e[m->n].name = xstrdup(name);
    m->e[m->n].value = xstrdup(value);
    m->n++;
}

static int smap_get(const SMap *m, const char *name, const char **out)
{
    for (size_t k = 0; k < m->n; k++) {
        if (strcmp(m->e[k].name, name) == 0) { *out = m->e[k].value; return 1; }
    }
    return 0;
}

typedef struct { char **v; size_t n, cap; } List;

static void list_addn(List *l, const char *s, size_t n)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = xrealloc(l->v, l->cap * sizeof(char *));
    }
    l->v[l->n++] = xstrndup(s, n);
}

static void list_add(List *l, const char *s) { list_addn(l, s, strlen(s)); }

/* input file split into lines (content without the final newline) */
typedef struct { const char *s; size_t n; } Line;
typedef struct { char *data; size_t size; Line *lines; size_t count; } Src;

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
        fclose(fp); free(data); return -1;
    }
    fclose(fp);
    data[sz] = '\0';

    count = 0;
    for (i = 0; i < (size_t)sz; i++) if (data[i] == '\n') count++;
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
/* Perl regex equivalents                                                */
/* -------------------------------------------------------------------- */

static size_t lead(const Line *l)
{
    size_t i = 0;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
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
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    return i >= l->n;
}

static int blank_line(const Line *l) { return at_end(l, 0); }
static int comment_line(const Line *l) { return l->n > 0 && l->s[0] == '#'; }
static int percent_line(const Line *l)
{
    size_t i = lead(l);
    return i < l->n && l->s[i] == '%';
}

/* ^\s* % \s* KW \s+ (\S+) \s*$ */
static int match_pct_capture(const Line *l, const char *kw, const char **cap, size_t *clen)
{
    size_t i = lead(l), s;

    if (i >= l->n || l->s[i] != '%') return 0;
    i++;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    if (!lit(l, &i, kw)) return 0;
    if (i >= l->n || !sp((unsigned char)l->s[i])) return 0;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    s = i;
    while (i < l->n && !sp((unsigned char)l->s[i])) i++;
    if (i == s) return 0;
    *cap = l->s + s;
    *clen = i - s;
    return at_end(l, i);
}

/* ^\s* % \s* KW \s*$ */
static int match_pct_only(const Line *l, const char *kw)
{
    size_t i = lead(l);

    if (i >= l->n || l->s[i] != '%') return 0;
    i++;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    return lit(l, &i, kw) && at_end(l, i);
}

/* (\w+):\s*(\S+)\s*$ -- unanchored, leftmost match like Perl */
static int parse_op_assign(const Line *l, char **codeid, char **opstr)
{
    for (size_t start = 0; start < l->n; start++) {
        size_t i, s, e, vs;

        if (!wd((unsigned char)l->s[start])) continue;
        s = start;
        i = start;
        while (i < l->n && wd((unsigned char)l->s[i])) i++;
        e = i;
        if (i >= l->n || l->s[i] != ':') continue;
        i++;
        while (i < l->n && sp((unsigned char)l->s[i])) i++;
        vs = i;
        while (i < l->n && !sp((unsigned char)l->s[i])) i++;
        if (i == vs) continue;
        if (!at_end(l, i)) continue;
        *codeid = xstrndup(l->s + s, e - s);
        *opstr = xstrndup(l->s + vs, i - vs);
        return 1;
    }
    return 0;
}

/* (\w+):\s*(.+)\s+(\d+)\s+(\d+)\s*$ -- unanchored, leftmost match.
   The greedy "(.+)" keeps every whitespace character but the last one. */
static int parse_function_assign(const Line *l, char **codeid, char **name,
                                 char **minargs, char **maxargs)
{
    for (size_t start = 0; start < l->n; start++) {
        size_t i, s, e, num2s, num1s, nameend, k, nd_end;

        if (!wd((unsigned char)l->s[start])) continue;
        s = start;
        i = start;
        while (i < l->n && wd((unsigned char)l->s[i])) i++;
        e = i;
        if (i >= l->n || l->s[i] != ':') continue;
        i++;
        while (i < l->n && sp((unsigned char)l->s[i])) i++;
        if (i >= l->n) continue;

        k = l->n;
        while (k > i && sp((unsigned char)l->s[k - 1])) k--;
        nd_end = k;
        num2s = k;
        while (num2s > i && l->s[num2s - 1] >= '0' && l->s[num2s - 1] <= '9') num2s--;
        if (num2s == k) continue;

        k = num2s;
        while (k > i && sp((unsigned char)l->s[k - 1])) k--;
        if (k == num2s) continue;

        num1s = k;
        while (num1s > i && l->s[num1s - 1] >= '0' && l->s[num1s - 1] <= '9') num1s--;
        if (num1s == k || num1s == 0) continue;
        if (!sp((unsigned char)l->s[num1s - 1])) continue;

        nameend = num1s - 1;
        if (nameend <= i) continue;

        *codeid = xstrndup(l->s + s, e - s);
        *name = xstrndup(l->s + i, nameend - i);
        *minargs = xstrndup(l->s + num1s, k - num1s);
        *maxargs = xstrndup(l->s + num2s, nd_end - num2s);
        return 1;
    }
    return 0;
}

/* ^([\w/]+):\s*(.*)$ */
static int parse_command_line(const Line *l, char **opcode_part, char **syntax)
{
    size_t i = 0, s;

    while (i < l->n && (wd((unsigned char)l->s[i]) || l->s[i] == '/')) i++;
    if (i == 0) return 0;
    s = i;
    if (i >= l->n || l->s[i] != ':') return 0;
    i++;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    *opcode_part = xstrndup(l->s, s);
    *syntax = xstrndup(l->s + i, l->n - i);
    return 1;
}

/* Perl split /\s+/: leading empty field kept, trailing empty fields dropped,
   a string made only of separators gives an empty list */
static void split_ws(const char *s, size_t n, List *out)
{
    size_t i = 0;

    while (i < n) {
        size_t start = i;
        while (i < n && !sp((unsigned char)s[i])) i++;
        list_addn(out, s + start, i - start);
        while (i < n && sp((unsigned char)s[i])) i++;
    }
    if (out->n == 1 && out->v[0][0] == '\0') out->n = 0;
}

/* Perl split /X/ for a single character separator */
static void split_chr(const char *s, size_t n, char sep, List *out)
{
    size_t i = 0;

    while (i < n) {
        size_t start = i;
        while (i < n && s[i] != sep) i++;
        list_addn(out, s + start, i - start);
        if (i < n) i++;
    }
    if (out->n == 1 && out->v[0][0] == '\0') out->n = 0;
}

/* -------------------------------------------------------------------- */
/* generation state                                                      */
/* -------------------------------------------------------------------- */

typedef struct {
    char  *type;
    char  *value;       /* the text that gets printed */
    char  *raw;         /* EX_LEX_CHARACTER: the raw character(s) */
    char  *lexeme;      /* EX_LEX_NSYMBOL: the BASIC symbol text */
    char **go;
    size_t ngo;
} Syn;

typedef struct {
    char *opcode;
    char *function;
    Syn  *syn;
    size_t nsyn, capsyn;
} Cmd;

typedef struct { char *opcode, *name; long prec; } Op;
typedef struct { char *opcode, *name, *minargs, *maxargs; } Fun;
typedef struct { char *name, *value; } Const;

static Map   COMMANDS;      /* opcode -> code */
static Map   NOEXEC;        /* opcode -> flag */
static Map   SYMBOL;        /* BASIC symbol -> code */
static SMap  ANASYMBOLS;    /* operator name -> opcode */

static Cmd  *CMDS;  static size_t NCMDS, CAPCMDS;
static Op   *BINS;  static size_t NBINS, CAPBINS;
static Op   *UNAS;  static size_t NUNAS, CAPUNAS;
static Fun  *FUNS;  static size_t NFUNS, CAPFUNS;
static List  NASYMS, ASYMS;
static Const *CONSTS; static size_t NCONSTS, CAPCONSTS;

static long SymbolCounter = 256;
static long Precedence = 1;
static char *CFILE_N;
static char *HFILE_N;
static Buf   OUT;

static int noexec_of(const char *opcode)
{
    long v;
    return map_get(&NOEXEC, opcode, &v) && v;
}

static Cmd *cmd_begin(const char *opcode, const char *function)
{
    Cmd *c;

    if (NCMDS == CAPCMDS) {
        CAPCMDS = CAPCMDS ? CAPCMDS * 2 : 64;
        CMDS = xrealloc(CMDS, CAPCMDS * sizeof(Cmd));
    }
    c = &CMDS[NCMDS++];
    memset(c, 0, sizeof(*c));
    c->opcode = xstrdup(opcode ? opcode : "");
    c->function = xstrdup(function ? function : "");
    return c;
}

static void op_add(Op **arr, size_t *n, size_t *cap, const char *opcode,
                   const char *name, long prec)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *arr = xrealloc(*arr, *cap * sizeof(Op));
    }
    (*arr)[*n].opcode = xstrdup(opcode ? opcode : "");
    (*arr)[*n].name = xstrdup(name ? name : "");
    (*arr)[*n].prec = prec;
    (*n)++;
}

static void fun_add(const char *opcode, const char *name,
                    const char *minargs, const char *maxargs)
{
    if (NFUNS == CAPFUNS) {
        CAPFUNS = CAPFUNS ? CAPFUNS * 2 : 64;
        FUNS = xrealloc(FUNS, CAPFUNS * sizeof(Fun));
    }
    FUNS[NFUNS].opcode = xstrdup(opcode ? opcode : "");
    FUNS[NFUNS].name = xstrdup(name ? name : "");
    FUNS[NFUNS].minargs = xstrdup(minargs ? minargs : "");
    FUNS[NFUNS].maxargs = xstrdup(maxargs ? maxargs : "");
    NFUNS++;
}

static void const_add(const char *name, const char *value)
{
    if (NCONSTS == CAPCONSTS) {
        CAPCONSTS = CAPCONSTS ? CAPCONSTS * 2 : 64;
        CONSTS = xrealloc(CONSTS, CAPCONSTS * sizeof(Const));
    }
    CONSTS[NCONSTS].name = xstrdup(name ? name : "");
    CONSTS[NCONSTS].value = xstrdup(value ? value : "");
    NCONSTS++;
}

static void syn_add(Cmd *c, const char *type, const char *value,
                    const char *raw, const char *lexeme, List *go)
{
    Syn *s;

    if (c->nsyn == c->capsyn) {
        c->capsyn = c->capsyn ? c->capsyn * 2 : 8;
        c->syn = xrealloc(c->syn, c->capsyn * sizeof(Syn));
    }
    s = &c->syn[c->nsyn++];
    memset(s, 0, sizeof(*s));
    s->type = xstrdup(type);
    s->value = xstrdup(value ? value : "");
    s->raw = raw ? xstrdup(raw) : NULL;
    s->lexeme = lexeme ? xstrdup(lexeme) : NULL;
    if (go && go->n) {
        s->go = xmalloc(go->n * sizeof(char *));
        for (size_t k = 0; k < go->n; k++) s->go[k] = xstrdup(go->v[k]);
        s->ngo = go->n;
    }
}

/* -------------------------------------------------------------------- */
/* parsing                                                               */
/* -------------------------------------------------------------------- */

static int constant_line(const Line *l)
{
    size_t i, s, e;

    if (l->n == 0 || l->s[0] != '!') return 0;
    i = 1;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    s = i;
    while (i < l->n && wd((unsigned char)l->s[i])) i++;
    e = i;
    if (e == s) return 0;
    if (i >= l->n || !sp((unsigned char)l->s[i])) return 0;
    while (i < l->n && sp((unsigned char)l->s[i])) i++;
    const_add(xstrndup(l->s + s, e - s), xstrndup(l->s + i, l->n - i));
    return 1;
}

static void syntax_token(Cmd *cmd, const char *tok, size_t n, int *noexec)
{
    char *raw = NULL, *lexeme = NULL, *value = NULL;
    const char *type = NULL;
    List go = {0};

    /* 'x' with a non word character */
    for (size_t i = 0; i + 2 < n; i++) {
        if (tok[i] == '\'' && !wd((unsigned char)tok[i + 1]) && tok[i + 2] == '\'') {
            raw = xstrndup(tok + i + 1, 1);
            type = "EX_LEX_CHARACTER";
            break;
        }
    }

    /* 'symbol' */
    if (!type) {
        size_t p = (size_t)-1, q = (size_t)-1;

        for (size_t i = 0; i < n; i++) if (tok[i] == '\'') { p = i; break; }
        for (size_t i = n; i > 0; i--) if (tok[i - 1] == '\'') { q = i - 1; break; }
        if (p != (size_t)-1 && q != (size_t)-1 && q > p + 1) {
            long symv;
            char *name = xstrndup(tok + p + 1, q - p - 1);

            if (!map_get(&SYMBOL, name, &symv)) {
                map_set(&SYMBOL, name, SymbolCounter);
                symv = SymbolCounter;
                SymbolCounter++;
            }
            type = "EX_LEX_NSYMBOL";
            lexeme = name;
            value = xmalloc(32);
            snprintf(value, 32, "%ld", symv);
        }
    }

    if (!type) {
        static const struct { const char *tok, *type; } kw[] = {
            {"nl", "EX_LEX_CHARACTER"},
            {"tab", "EX_LEX_CHARACTER"},
            {"integer", "EX_LEX_LONG"},
            {"label", "EX_LEX_LABEL"},
            {"arg_num", "EX_LEX_ARG_NUM"},
            {"label_def", "EX_LEX_LABEL_DEFINITION"},
            {"float", "EX_LEX_DOUBLE"},
            {"string", "EX_LEX_STRING"},
            {"absolute_symbol", "EX_LEX_ASYMBOL"},
            {"pragma", "EX_LEX_PRAGMA"},
            {"symbol", "EX_LEX_SYMBOL"},
            {"name_space", "EX_LEX_SET_NAME_SPACE"},
            {"end_name_space", "EX_LEX_RESET_NAME_SPACE"},
            {"expression", "EX_LEX_EXP"},
            {"expression_list", "EX_LEX_EXPL"},
            {"lval", "EX_LEX_LVAL"},
            {"lval_list", "EX_LEX_LVALL"},
            {"local_start", "EX_LEX_LOCAL_START"},
            {"local_end", "EX_LEX_LOCAL_END"},
            {"local", "EX_LEX_LOCAL"},
            {"global", "EX_LEX_GLOBAL"},
            {"noexec", "EX_LEX_NOEXEC"},
            {"*", "EX_LEX_STAR"},
            {"local_list", "EX_LEX_LOCALL"},
            {"global_list", "EX_LEX_GLOBALL"},
            {"thisfn", "EX_LEX_THIS_FUNCTION"},
            {"function", "EX_LEX_FUNCTION"},
            {"cname", "EX_LEX_CONST_NAME"},
            {"gcname", "EX_LEX_GCONST_NAME"},
            {"cval", "EX_LEX_CONST_VALUE"},
            {NULL, NULL}
        };

        for (size_t k = 0; kw[k].tok; k++) {
            if (strlen(kw[k].tok) == n && memcmp(kw[k].tok, tok, n) == 0) {
                if (strcmp(kw[k].tok, "nl") == 0) {
                    type = "EX_LEX_CHARACTER";
                    raw = xstrdup("\\n");
                } else if (strcmp(kw[k].tok, "tab") == 0) {
                    type = "EX_LEX_CHARACTER";
                    raw = xstrdup("\\t");
                } else {
                    type = kw[k].type;
                    if (strcmp(kw[k].tok, "noexec") == 0) *noexec = 1;
                }
                break;
            }
        }

        if (!type) {
            static const struct { const char *pre, *type; } gof[] = {
                {"go_back(", "EX_LEX_GO_BACK"},
                {"go_forward(", "EX_LEX_GO_FORWARD"},
                {"come_back(", "EX_LEX_COME_BACK"},
                {"come_forward(", "EX_LEX_COME_FORWARD"},
                {NULL, NULL}
            };

            for (size_t k = 0; gof[k].pre; k++) {
                size_t pl = strlen(gof[k].pre);
                if (n >= pl + 1 && memcmp(tok, gof[k].pre, pl) == 0 && tok[n - 1] == ')') {
                    split_chr(tok + pl, n - pl - 1, ',', &go);
                    type = gof[k].type;
                    break;
                }
            }
        }
    }

    if (!type) {
        printf("I have found an invalid symbol: %.*s\n", (int)n, tok);
        fputs("insy\n", stderr);
        exit(1);
    }
    if (!value) value = xstrdup("0");
    syn_add(cmd, type, value, raw, lexeme, &go);
}

static void do_commands(Src *f, size_t *pi)
{
    while (*pi < f->count) {
        const Line *l = &f->lines[*pi];
        char *code_part = NULL, *syntax = NULL;
        const char *function, *opcode;
        Cmd *cmd;
        List toks = {0}, parts = {0};
        int noexec = 0;

        if (percent_line(l)) return;
        if (comment_line(l) || blank_line(l) || constant_line(l)) { (*pi)++; continue; }

        if (!parse_command_line(l, &code_part, &syntax)) {
            code_part = xstrdup("");
            syntax = xstrdup("");
        }
        split_chr(code_part, strlen(code_part), '/', &parts);
        opcode = (parts.n >= 1) ? parts.v[0] : "";
        function = (parts.n >= 2) ? parts.v[1] : "";
        if (!function[0]) function = "This";

        map_set(&COMMANDS, opcode, 1);
        cmd = cmd_begin(opcode, function);
        split_ws(syntax, strlen(syntax), &toks);
        for (size_t k = 0; k < toks.n; k++)
            syntax_token(cmd, toks.v[k], strlen(toks.v[k]), &noexec);
        map_set(&NOEXEC, code_part, noexec);
        (*pi)++;
    }
}

static void do_unaries(Src *f, size_t *pi)
{
    while (*pi < f->count) {
        const Line *l = &f->lines[*pi];
        char *codeid = NULL, *opstr = NULL;

        if (percent_line(l)) return;
        if (comment_line(l) || blank_line(l) || constant_line(l)) { (*pi)++; continue; }

        parse_op_assign(l, &codeid, &opstr);
        if (codeid) map_set(&COMMANDS, codeid, 1);
        else map_set(&COMMANDS, "", 1);
        op_add(&UNAS, &NUNAS, &CAPUNAS, codeid, opstr, 0);
        (*pi)++;
    }
}

static void do_binaries(Src *f, size_t *pi)
{
    while (*pi < f->count) {
        const Line *l = &f->lines[*pi];
        char *codeid = NULL, *opstr = NULL;

        if (!comment_line(l) && !blank_line(l) && !constant_line(l)) {
            if (match_pct_only(l, "PRECEDENCE")) {
                Precedence++;
                (*pi)++;
                continue;
            }
            if (percent_line(l)) return;
            parse_op_assign(l, &codeid, &opstr);
            if (codeid) map_set(&COMMANDS, codeid, 1);
            else map_set(&COMMANDS, "", 1);
            op_add(&BINS, &NBINS, &CAPBINS, codeid, opstr, Precedence);
        }
        (*pi)++;
    }
}

static void do_functions(Src *f, size_t *pi)
{
    while (*pi < f->count) {
        const Line *l = &f->lines[*pi];
        char *codeid = NULL, *name = NULL, *minargs = NULL, *maxargs = NULL;

        if (percent_line(l)) return;
        if (comment_line(l) || blank_line(l) || constant_line(l)) { (*pi)++; continue; }

        parse_function_assign(l, &codeid, &name, &minargs, &maxargs);
        if (codeid) map_set(&COMMANDS, codeid, 1);
        else map_set(&COMMANDS, "", 1);
        fun_add(codeid, name, minargs, maxargs);
        (*pi)++;
    }
}

static int is_symbol_alpha(const char *s)
{
    if (!*s) return 0;
    for (const char *p = s; *p; p++) {
        if (!(wd((unsigned char)*p) || *p == '$' || *p == ':')) return 0;
    }
    return 1;
}

static void calculate_command_codes(void)
{
    for (size_t k = 0; k < COMMANDS.n; k++) {
        if (!noexec_of(COMMANDS.e[k].name))
            COMMANDS.e[k].value = SymbolCounter++;
    }
    for (size_t k = 0; k < COMMANDS.n; k++) {
        if (noexec_of(COMMANDS.e[k].name))
            COMMANDS.e[k].value = SymbolCounter++;
    }
}

static void collect_symbols(void)
{
    for (size_t k = 0; k < NBINS; k++) {
        if (is_symbol_alpha(BINS[k].name)) list_add(&ASYMS, BINS[k].name);
        else list_add(&NASYMS, BINS[k].name);
        smap_set(&ANASYMBOLS, BINS[k].name, BINS[k].opcode);
    }
    for (size_t k = 0; k < NUNAS; k++) {
        if (is_symbol_alpha(UNAS[k].name)) list_add(&ASYMS, UNAS[k].name);
        else list_add(&NASYMS, UNAS[k].name);
        smap_set(&ANASYMBOLS, UNAS[k].name, UNAS[k].opcode);
    }
    for (size_t k = 0; k < NFUNS; k++) {
        if (is_symbol_alpha(FUNS[k].name)) list_add(&ASYMS, FUNS[k].name);
        else list_add(&NASYMS, FUNS[k].name);
        smap_set(&ANASYMBOLS, FUNS[k].name, FUNS[k].opcode);
    }
    for (size_t k = 0; k < SYMBOL.n; k++) {
        if (is_symbol_alpha(SYMBOL.e[k].name)) list_add(&ASYMS, SYMBOL.e[k].name);
        else list_add(&NASYMS, SYMBOL.e[k].name);
    }
}

/* stable sort by length, longest first (Perl: sort { length($b) cmp length($a) }) */
static void sort_by_length(List *l)
{
    for (size_t i = 1; i < l->n; i++) {
        char *key = l->v[i];
        size_t kl = strlen(key);
        size_t j = i;

        while (j > 0 && strlen(l->v[j - 1]) < kl) {
            l->v[j] = l->v[j - 1];
            j--;
        }
        l->v[j] = key;
    }
}

static void correct_commands(void)
{
    for (size_t i = 0; i < NCMDS; i++) {
        Cmd *c = &CMDS[i];

        for (size_t k = 0; k < c->nsyn; k++) {
            Syn *s = &c->syn[k];
            const char *op;

            if (strcmp(s->type, "EX_LEX_NSYMBOL") == 0) {
                if (smap_get(&ANASYMBOLS, s->lexeme, &op)) {
                    free(s->value);
                    s->value = xmalloc(strlen(op) + 5);
                    sprintf(s->value, "CMD_%s", op);
                }
            } else if (strcmp(s->type, "EX_LEX_CHARACTER") == 0) {
                if (smap_get(&ANASYMBOLS, s->raw, &op)) {
                    free(s->type);
                    s->type = xstrdup("EX_LEX_NSYMBOL");
                    free(s->value);
                    s->value = xmalloc(strlen(op) + 5);
                    sprintf(s->value, "CMD_%s", op);
                } else {
                    char *v = xmalloc(strlen(s->raw) + 3);
                    sprintf(v, "'%s'", s->raw);
                    free(s->value);
                    s->value = v;
                }
            }
        }
    }
}

/* -------------------------------------------------------------------- */
/* output                                                                */
/* -------------------------------------------------------------------- */

static void write_if_different(const char *path, const Buf *content)
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
        fprintf(stderr, "Cannot open %s\n", path);
        exit(1);
    }
    if (content->len) fwrite(content->buf, 1, content->len, fp);
    fclose(fp);
}

static void print_cfile_start(void)
{
    buf_str(&OUT, "/*\nFILE: ");
    buf_str(&OUT, CFILE_N ? CFILE_N : "");
    buf_str(&OUT, "\nHEADER: ");
    buf_str(&OUT, HFILE_N ? HFILE_N : "");
    buf_str(&OUT, "\n\nThis file was automatically generated from syntax.def by syntaxer.pl\n"
                  "You better do not edit this file. Modify syntax.def and run syntaxer.pl again.\n\n*/\n"
                  "#include <stdlib.h>\n#include <stdio.h>\n#include \"report.h\"\n#include \"lexer.h\"\n"
                  "#include \"sym.h\"\n#include \"expression.h\"\n#include \"syntax.h\"\n\n");
}

static void print_hfile_start(void)
{
    Precedence++;
    buf_printf(&OUT, "/*\nTO_HEADER:\n#define MAX_BINARY_OPERATOR_PRECEDENCE %ld\n*/\n", Precedence);
}

static void print_command_defines(void)
{
    long startval = 0, endval = 0;
    int first = 1;

    for (size_t k = 0; k < COMMANDS.n; k++) {
        long v = COMMANDS.e[k].value;
        if (first || v < startval) startval = v;
        if (first || v > endval) endval = v;
        first = 0;
    }
    buf_str(&OUT, "/*\nTO_HEADER:\n");
    buf_printf(&OUT, "#define START_CMD %ld\n", startval);
    buf_printf(&OUT, "#define END_CMD %ld\n", endval);
    buf_printf(&OUT, "#define NUM_CMD %ld\n", endval - startval + 1);
    buf_str(&OUT, "\n#include \"memory.h\"\n#include \"builder.h\"\n#include \"execute.h\"\n\n");
    for (size_t k = 0; k < COMMANDS.n; k++) {
        buf_printf(&OUT, "#define CMD_%s %ld\n", COMMANDS.e[k].name, COMMANDS.e[k].value);
        if (!noexec_of(COMMANDS.e[k].name))
            buf_printf(&OUT, "void COMMAND_%s(pExecuteObject);\n", COMMANDS.e[k].name);
    }
    buf_str(&OUT, "*/\n");
}

static void print_binaries(void)
{
    buf_str(&OUT, "unsigned long BINARIES[] = {\n");
    for (size_t k = 0; k < NBINS; k++)
        buf_printf(&OUT, "  CMD_%s , %ld ,\n", BINS[k].opcode, BINS[k].prec);
    buf_str(&OUT, " 0\n };\n");
}

static void print_unaries(void)
{
    buf_str(&OUT, "unsigned long UNARIES[] = {\n");
    for (size_t k = 0; k < NUNAS; k++)
        buf_printf(&OUT, "CMD_%s ,\n", UNAS[k].opcode);
    buf_str(&OUT, " 0 };\n");
}

static void print_function_table(void)
{
    buf_str(&OUT, "BFun INTERNALFUNCTIONS[] = {\n");
    for (size_t k = 0; k < NFUNS; k++)
        buf_printf(&OUT, "{ CMD_%s , %s , %s}, /*%s*/\n",
                   FUNS[k].opcode, FUNS[k].minargs, FUNS[k].maxargs, FUNS[k].name);
    buf_str(&OUT, "  { 0, 0, 0 }\n };\n");
}

static const char *symbol_value(const char *name)
{
    static char buf[512];
    const char *op;
    long v;

    if (smap_get(&ANASYMBOLS, name, &op)) {
        snprintf(buf, sizeof(buf), "CMD_%s", op);
        return buf;
    }
    if (map_get(&SYMBOL, name, &v)) {
        snprintf(buf, sizeof(buf), "%ld", v);
        return buf;
    }
    return "";
}

static void print_nasymbols(void)
{
    buf_str(&OUT, "LexNASymbol NASYMBOLS[] = {\n");
    for (size_t k = 0; k < NASYMS.n; k++) {
        int printed = 0;

        for (size_t j = 0; j < k; j++) {
            if (strcmp(NASYMS.v[j], NASYMS.v[k]) == 0) { printed = 1; break; }
        }
        if (printed) continue;
        buf_str(&OUT, "{ \"");
        for (const char *p = NASYMS.v[k]; *p; p++) {
            if (*p == '\\') buf_str(&OUT, "\\\\");
            else buf_ch(&OUT, *p);
        }
        buf_printf(&OUT, "\" , %s } ,\n", symbol_value(NASYMS.v[k]));
    }
    buf_str(&OUT, "{ NULL, 0 }\n  };\n");
}

static void print_asymbols(void)
{
    buf_str(&OUT, "LexNASymbol ASYMBOLS[] = {\n");
    for (size_t k = 0; k < ASYMS.n; k++) {
        int printed = 0;
        char *kw;

        for (size_t j = 0; j < k; j++) {
            if (strcmp(ASYMS.v[j], ASYMS.v[k]) == 0) { printed = 1; break; }
        }
        if (printed) continue;
        kw = xstrdup(ASYMS.v[k]);
        for (char *p = kw; *p; p++) {
            int c = (unsigned char)*p;
            c = (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
            if (c == '$') c = 'S';
            *p = (char)c;
        }
        buf_printf(&OUT, "/*\nTO_HEADER:\n#define KEYWORDCODE_%s %s\n*/\n",
                   kw, symbol_value(ASYMS.v[k]));
        buf_printf(&OUT, "{ \"%s\" , %s } ,\n", ASYMS.v[k], symbol_value(ASYMS.v[k]));
        free(kw);
    }
    buf_str(&OUT, "{ NULL, 0 }\n  };\n");
}

static void print_csymbols(void)
{
    buf_str(&OUT, "LexNASymbol CSYMBOLS[] = {\n");
    for (size_t k = 0; k < COMMANDS.n; k++)
        buf_printf(&OUT, "{ \"%s\" , CMD_%s } , /* %ld */\n",
                   COMMANDS.e[k].name, COMMANDS.e[k].name, COMMANDS.e[k].value);
    buf_str(&OUT, "{ NULL, 0 }\n  };\n");
}

static void print_externs(void)
{
    buf_str(&OUT, "/*\nTO_HEADER:\n"
                  "extern PredLConst PREDLCONSTS[];\n"
                  "extern unsigned long BINARIES[];\n"
                  "extern unsigned long UNARIES[];\n"
                  "extern BFun INTERNALFUNCTIONS[];\n"
                  "extern LineSyntax COMMANDS[];\n"
                  "extern LexNASymbol NASYMBOLS[];\n"
                  "extern LexNASymbol ASYMBOLS[];\n"
                  "extern LexNASymbol CSYMBOLS[];\n"
                  "extern char *COMMANDSYMBOLS[];\n"
                  "extern CommandFunctionType CommandFunction[];\n"
                  "*/\n");
}

static void print_command_table(void)
{
    buf_str(&OUT, "LineSyntax COMMANDS[] = {\n");
    for (size_t i = 0; i < NCMDS; i++) {
        Cmd *c = &CMDS[i];

        buf_printf(&OUT, "{ CMD_%s , ex_IsCommand%s, \n {\n", c->opcode, c->function);
        for (size_t k = 0; k < c->nsyn; k++) {
            Syn *s = &c->syn[k];

            buf_printf(&OUT, "  { %s , %s", s->type, s->value);
            for (size_t g = 0; g < s->ngo; g++)
                buf_printf(&OUT, ", CMD_%s ", s->go[g]);
            buf_str(&OUT, " },\n");
        }
        buf_str(&OUT, "  { 0 ,  0 }\n }\n},\n");
    }
    buf_str(&OUT, "  { 0 , NULL    , { { 0 , 0}}}\n");
    buf_str(&OUT, "};\n");
}

static void print_command_function_table(void)
{
    long pval = 0;
    int defined = 0;

    buf_str(&OUT, "CommandFunctionType CommandFunction[]={\n");
    for (size_t k = 0; k < COMMANDS.n; k++) {
        long v;

        if (noexec_of(COMMANDS.e[k].name)) continue;
        v = COMMANDS.e[k].value;
        buf_printf(&OUT, "  COMMAND_%s, /* %ld */\n", COMMANDS.e[k].name, v);
        if (defined && pval + 1 != v) {
            fprintf(stderr, "failed hash order: %ld %ld\n", pval, v);
            exit(1);
        }
        pval = v;
        defined = 1;
    }
    buf_str(&OUT, "  NULL\n  };\n");
    pval++;
    buf_printf(&OUT, "/*\nTO_HEADER:\n#define END_EXEC %ld\n*/\n", pval);
}

static void print_command_symbol_table(void)
{
    long pval = 0;
    int defined = 0;

    buf_str(&OUT, "char *COMMANDSYMBOLS[]={\n");
    for (size_t k = 0; k < COMMANDS.n; k++) {
        long v;

        if (noexec_of(COMMANDS.e[k].name)) continue;
        v = COMMANDS.e[k].value;
        buf_printf(&OUT, "         \"%s\", /* %ld */\n", COMMANDS.e[k].name, v);
        if (defined && pval + 1 != v) {
            fprintf(stderr, "failed hash order: %ld %ld\n", pval, v);
            exit(1);
        }
        pval = v;
        defined = 1;
    }
    buf_str(&OUT, "  NULL\n  };\n");
}

static void print_consts(void)
{
    buf_str(&OUT, "PredLConst PREDLCONSTS[]={\n");
    for (size_t k = NCONSTS; k > 0; k--) {
        const Const *c = &CONSTS[k - 1];
        char *lc = xstrdup(c->name);

        for (char *p = lc; *p; p++) *p = (char)ascii_lower((unsigned char)*p);
        buf_printf(&OUT, "  { \"%s\",%s },\n", lc, c->value);
        free(lc);
    }
    buf_str(&OUT, "  { NULL , 0L }\n};");
}

static void print_notimp_table(void)
{
    Buf b;
    FILE *fp;

    buf_init(&b);
    buf_str(&b, "/* FILE: notimp.c\n"
                "\n"
                "   This file was automatically generated by the program syntaxer.pl\n"
                "\n"
                "   If you alter this file setting any of the symbols to 1 instead of zero\n"
                "   the corresponding command will not be implemented into the next compilation\n"
                "   of ScriptBasic. Running a BASIC program containing an instruction not implemented\n"
                "   will raise an error than can be captured by ON ERROR GOTO unless ON ERROR GOTO is\n"
                "   also unimplemented.\n"
                "\n"
                "TO_HEADER:\n"
                "\n");
    for (size_t k = 0; k < NCMDS; k++)
        buf_printf(&b, "#define NOTIMP_%s 0\n", CMDS[k].opcode);
    for (size_t k = 0; k < NFUNS; k++)
        buf_printf(&b, "#define NOTIMP_%s 0\n", FUNS[k].opcode);
    buf_str(&b, "#include \"mynotimp.h\"\n*/\n");

    fp = fopen("notimp.c", "wb");
    if (!fp) {
        fputs("Can not open notimp.c for output\n", stderr);
        exit(1);
    }
    if (b.len) fwrite(b.buf, 1, b.len, fp);
    fclose(fp);
}

/* -------------------------------------------------------------------- */
/* main                                                                  */
/* -------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *in_file = (argc > 1) ? argv[1] : "syntax.def";
    Src f;
    size_t i;

    if (src_read(in_file, &f) != 0) {
        fprintf(stderr, "Can not open %s\n", in_file);
        exit(1);
    }

    CFILE_N = xstrdup("syntax.c");

    i = 0;
    while (i < f.count) {
        const Line *l = &f.lines[i];
        const char *cap;
        size_t clen;

        if (comment_line(l) || blank_line(l)) { i++; continue; }

        if (match_pct_capture(l, "NUMSTART", &cap, &clen)) {
            char *tmp = xstrndup(cap, clen);
            SymbolCounter = strtol(tmp, NULL, 10);
            free(tmp);
            i++;
            continue;
        }
        if (match_pct_capture(l, "CFILE", &cap, &clen)) {
            free(CFILE_N);
            CFILE_N = xstrndup(cap, clen);
            i++;
            continue;
        }
        if (match_pct_capture(l, "FILE", &cap, &clen)) {
            char *base = xstrndup(cap, clen);

            free(CFILE_N);
            CFILE_N = xmalloc(strlen(base) + 3);
            sprintf(CFILE_N, "%s.c", base);
            free(HFILE_N);
            HFILE_N = xmalloc(strlen(base) + 3);
            sprintf(HFILE_N, "%s.h", base);
            free(base);
            i++;
            continue;
        }
        if (match_pct_only(l, "COMMANDS"))  { i++; do_commands(&f, &i);  continue; }
        if (match_pct_only(l, "UNARIES"))   { i++; do_unaries(&f, &i);   continue; }
        if (match_pct_only(l, "BINARIES"))  { i++; do_binaries(&f, &i);  continue; }
        if (match_pct_only(l, "FUNCTIONS")) { i++; do_functions(&f, &i); continue; }
        i++;
    }

    buf_init(&OUT);

    calculate_command_codes();
    collect_symbols();
    correct_commands();

    sort_by_length(&NASYMS);
    sort_by_length(&ASYMS);

    print_cfile_start();
    print_hfile_start();
    print_command_defines();
    print_binaries();
    print_unaries();
    print_function_table();
    print_command_table();
    print_nasymbols();
    print_asymbols();
    print_csymbols();
    print_externs();
    print_command_function_table();
    print_command_symbol_table();
    print_consts();

    write_if_different(CFILE_N, &OUT);
    print_notimp_table();
    return 0;
}
