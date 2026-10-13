/*
 * mkcdoc: the command reference of the User Guide and of the old web site,
 * from the documentation blocks of the C files of the src/commands directory.
 *
 * This is the C port of mkcdoc.pl.  The documentation of a command sits in a C
 * comment that opens with the name of the command and carries the directives
 * =section, =title, =subtitle, =display after it:
 *
 *     ABS
 *     =section math
 *     =display ABS()
 *
 *     Returns the absolute value of the argument ...
 *
 * and the text of the comment, the directives taken out, is the documentation
 * of the command.  mkcdoc walks the src/commands directory and writes
 * commands.texi, the "Command reference" chapter that texi/ug.texi.jam
 * includes, and, when it is asked to, the HTML pages of the web site
 * (commands.html and one page for every command that has a =details line).
 *
 * The port follows the Perl step by step, the careless places included:
 *
 *  * a command that is written twice keeps the text of both, and a =title, a
 *    =subtitle or a =display that is written twice is reported on the error
 *    output and the second one wins.
 *
 * Two things of the Perl are left out, both of them careless places that only
 * show up on UNIX, where the sources are read in binary mode:
 *
 *  * the name of a command, its title, its subtitle and its display are the
 *    rest of the line with the line feed taken off and nothing else in the
 *    Perl, so the CR of the CRLF sources stays in them there: its chapter has
 *    a CR at the end of every "@section" line and its detail pages are named
 *    with a CR before the .html.  The CR goes off with the LF here, which is
 *    what the same program does on Windows, where a line read in text mode has
 *    no CR, and the whole output has one line ending and not two;
 *  * the Perl writes "@b{...}>" and "@emph{...}>" with the closing > of the
 *    pattern left behind, which shows in the three places where the sources use
 *    B<> or I<> (B<empty>, B<after>, I<root>).  The > is not written here.
 *
 * The escaping of @, $, { and } still runs after the HTML tags <tt>, <UL> and
 * <LI> were turned into texinfo, so a <tt> would come out as "@@code@{"; the
 * sources of this tree use T<> and R<> and no <tt>, so it does not show, and
 * the output of this port is the output of the Perl byte for byte after the CR
 * of the two files are taken off, apart from those three places.
 *
 * usage: mkcdoc [<option> ...]
 *
 *   --commands <dir>   the directory of the C files, "src/commands" by default
 *   --texi <file>      the file to write, "commands.texi" by default
 *   --html <dir>       write the HTML pages into <dir> as well.  The Perl
 *                      program always wrote them into ../html, the directory
 *                      of the old web site; that web site is history now, so
 *                      nothing is written unless this option is given.
 */
#define _POSIX_C_SOURCE 200809L   /* getline, strncasecmp */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ----------------------------------------------------------------- helpers */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "mkcdoc: out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "mkcdoc: out of memory\n"); exit(1); }
    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

typedef struct { char *p; size_t len, cap; } SB;

static void sb_init(SB *b) { b->cap = 64; b->len = 0; b->p = xmalloc(b->cap); b->p[0] = '\0'; }

static void sb_need(SB *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->p = xrealloc(b->p, b->cap);
    }
}

static void sb_puts(SB *b, const char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    sb_need(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_putc(SB *b, char c)
{
    sb_need(b, 1);
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
}

static char *sb_str(SB *b) { return b->p; }

/* give the string of the buffer away and empty the buffer */
static char *sb_take(SB *b) { char *p = b->p; b->p = NULL; b->len = b->cap = 0; return p; }

static void sb_free(SB *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

/* -------------------------------------------------------------- the commands */

typedef struct {
    char  *name;
    char **line;
    int     nline, cline;
    char  *title, *subtitle, *display;
} Command;

static Command *cmd = NULL;
static int      ncmd = 0, ccmd = 0;

typedef struct {
    char  *name;
    char **command;
    int     ncommand, ccommand;
} Section;

static Section *sect = NULL;
static int      nsect = 0, csect = 0;

static int cmp_str(const void *a, const void *b) { return strcmp(*(char * const *)a, *(char * const *)b); }

static Command *get_command(const char *name)
{
    for (int i = 0; i < ncmd; i++)
        if (strcmp(cmd[i].name, name) == 0) return &cmd[i];
    if (ncmd == ccmd) { ccmd = ccmd ? ccmd * 2 : 64; cmd = xrealloc(cmd, (size_t)ccmd * sizeof *cmd); }
    Command *c = &cmd[ncmd++];
    memset(c, 0, sizeof *c);
    c->name = xstrdup(name);
    return c;
}

static const char *display_of(const char *name)
{
    for (int i = 0; i < ncmd; i++)
        if (strcmp(cmd[i].name, name) == 0 && cmd[i].display) return cmd[i].display;
    return name;
}

static void command_add_line(Command *c, const char *line)
{
    if (c->nline == c->cline) {
        c->cline = c->cline ? c->cline * 2 : 16;
        c->line = xrealloc(c->line, (size_t)c->cline * sizeof *c->line);
    }
    c->line[c->nline++] = xstrdup(line);
}

static Section *get_section(const char *name)
{
    for (int i = 0; i < nsect; i++)
        if (strcmp(sect[i].name, name) == 0) return &sect[i];
    if (nsect == csect) { csect = csect ? csect * 2 : 32; sect = xrealloc(sect, (size_t)csect * sizeof *sect); }
    Section *s = &sect[nsect++];
    memset(s, 0, sizeof *s);
    s->name = xstrdup(name);
    return s;
}

static void section_add(Section *s, const char *command_name)
{
    if (s->ncommand == s->ccommand) {
        s->ccommand = s->ccommand ? s->ccommand * 2 : 8;
        s->command = xrealloc(s->command, (size_t)s->ccommand * sizeof *s->command);
    }
    s->command[s->ncommand++] = xstrdup(command_name);
}

static char **sorted_commands(int *count)
{
    char **v = xmalloc((size_t)(ncmd ? ncmd : 1) * sizeof *v);
    for (int i = 0; i < ncmd; i++) v[i] = cmd[i].name;
    qsort(v, (size_t)ncmd, sizeof *v, cmp_str);
    *count = ncmd;
    return v;
}

static char **sorted_sections(int *count)
{
    char **v = xmalloc((size_t)(nsect ? nsect : 1) * sizeof *v);
    for (int i = 0; i < nsect; i++) v[i] = sect[i].name;
    qsort(v, (size_t)nsect, sizeof *v, cmp_str);
    *count = nsect;
    return v;
}

/* -------------------------------------------------------------- the reading */

static int is_command_start(const char *line) { return line[0] == '/' && line[1] == '*' && line[2] == '*'; }
static int is_command_end(const char *line)   { return line[0] == '*' && line[1] == '/'; }

static int is_blank(const char *line)
{
    for (const char *p = line; *p; p++)
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return 0;
    return 1;
}

/* ^\s*=word\s*$ : the word alone on the line, whitespace around it allowed */
static int line_is(const char *line, const char *word)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    size_t n = strlen(word);
    if (strncmp(p, word, n) != 0) return 0;
    p += n;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0';
}

/* the rest of the line after the first skip characters, without the line
 * ending.  The Perl takes the LF off and nothing else, so the CR of the CRLF
 * sources of this tree stays in the name of a command and in the anchors and
 * the file names of the HTML output ("@section ABS" with a CR at the end of
 * the line, detail pages named "ABS" with a CR before the .html); this port
 * takes the CR off as well, which is what the Perl did on Windows, where a
 * line read in text mode has no CR. */
static char *rest_of_line(const char *line, size_t skip)
{
    /* \s+ of the Perl pattern is greedy: every space and tab after the
     * directive is taken, and then (.*) takes the rest of the line */
    while (line[skip] == ' ' || line[skip] == '\t') skip++;
    size_t len = strlen(line);
    while (len > skip && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    char *s = xmalloc(len - skip + 1);
    memcpy(s, line + skip, len - skip);
    s[len - skip] = '\0';
    return s;
}

static int has_directive(const char *line, const char *directive)
{
    size_t n = strlen(directive);
    if (strncmp(line, directive, n) != 0) return 0;
    return line[n] == ' ' || line[n] == '\t';
}

static void process_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    Command *actual = NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, f)) > 0) {
        (void)len;
        if (is_command_start(line)) {
            if (actual) {
                fprintf(stderr, "The command %s was not closed. Closing implicitly.\n", actual->name);
                actual = NULL;
            }
            char *name = rest_of_line(line, 3);
            actual = get_command(name);
            free(name);
            continue;
        }
        if (!actual) continue;
        if (is_command_end(line)) { actual = NULL; continue; }

        if (has_directive(line, "=section")) {
            /* the Perl splits the rest on whitespace, so the CR that is in the
             * rest of a CRLF line is not part of the name of a section */
            char *rest = rest_of_line(line, 8);
            for (char *w = strtok(rest, " \t\r\n"); w; w = strtok(NULL, " \t\r\n"))
                section_add(get_section(w), actual->name);
            free(rest);
            continue;
        }
        if (has_directive(line, "=title")) {
            char *value = rest_of_line(line, 6);
            if (actual->title)
                fprintf(stderr, "Title is double defined for %s\n   %s\n   %s\n",
                        actual->name, actual->title, value);
            actual->title = value;
            continue;
        }
        if (has_directive(line, "=subtitle")) {
            char *value = rest_of_line(line, 9);
            if (actual->subtitle)
                fprintf(stderr, "Subtitle is double defined for %s\n", actual->name);
            actual->subtitle = value;
            continue;
        }
        if (has_directive(line, "=display")) {
            char *value = rest_of_line(line, 8);
            if (actual->display)
                fprintf(stderr, "Title is double defined for %s\n", actual->name);
            actual->display = value;
            continue;
        }
        /* the line as it is, with the CR of a CRLF line taken off, so that
         * the output has one line ending and not two */
        if (strncmp(line, "\r\n", 2) == 0) command_add_line(actual, "\n");
        else if (line[0] == '\r' && line[1] == '\0') command_add_line(actual, "");
        else {
            size_t n = strlen(line);
            if (n && line[n - 1] == '\n' && n > 1 && line[n - 2] == '\r') {
                char *copy = xstrdup(line);
                copy[n - 2] = '\n';
                copy[n - 1] = '\0';
                command_add_line(actual, copy);
                free(copy);
            } else command_add_line(actual, line);
        }
    }
    free(line);
    fclose(f);
}

/* ------------------------------------------------------- the substitutions */

/* a literal replacement of every occurrence, case blind: Perl's s///gi */
static void append_replaced_ci(SB *out, const char *s, const char *needle, const char *repl)
{
    size_t nl = strlen(needle);
    for ( ; *s; ) {
        if (strncasecmp(s, needle, nl) == 0) { sb_puts(out, repl); s += nl; continue; }
        sb_putc(out, *s++);
    }
}

static char *replace_list_ci(const char *s, const char *const *from, const char *const *to, int n)
{
    char *cur = xstrdup(s);
    for (int i = 0; i < n; i++) {
        SB out; sb_init(&out);
        append_replaced_ci(&out, cur, from[i], to[i]);
        free(cur);
        cur = sb_take(&out);
    }
    return cur;
}

/* the escapes of the texinfo output: @ -> @@, $ -> @$, { -> @{, } -> @} */
static void append_escaped(SB *out, const char *s)
{
    for ( ; *s; s++) {
        if (*s == '@') sb_puts(out, "@@");
        else if (*s == '$') sb_puts(out, "@$");
        else if (*s == '{') sb_puts(out, "@{");
        else if (*s == '}') sb_puts(out, "@}");
        else sb_putc(out, *s);
    }
}

/* <a href=".*?">(.*?)</a> -> @xref{...} : Perl's
 * s[<a href=".*?">(.*?)</a>][\@xref{$1}]gi , the dots not greedy */
static void append_anchors(SB *out, const char *s)
{
    for ( ; *s; ) {
        if (strncasecmp(s, "<a href=\"", 9) == 0) {
            const char *gt = s + 9;
            while (*gt && strncmp(gt, "\">", 2) != 0) gt++;
            if (*gt) {
                const char *inner = gt + 2;
                const char *end = inner;
                while (*end && strncasecmp(end, "</a>", 4) != 0) end++;
                if (*end) {
                    sb_puts(out, "@xref{");
                    for (const char *p = inner; p < end; p++) sb_putc(out, *p);
                    sb_putc(out, '}');
                    s = end + 4;
                    continue;
                }
            }
        }
        sb_putc(out, *s++);
    }
}

/* the replacement of the text inside T<...>, B<...> or I<...> */
typedef void (*EmitFn)(SB *out, const char *inner, void *data);

/* Perl's s[<tag><(.+?)>][...]g : the shortest text up to the closing > */
static void append_tag(SB *out, const char *s, char tag, EmitFn emit, void *data)
{
    for ( ; *s; ) {
        if (s[0] == tag && s[1] == '<') {
            const char *e = s + 2;
            while (*e && *e != '>') e++;
            if (*e == '>') {
                size_t n = (size_t)(e - s - 2);
                char *inner = xmalloc(n + 1);
                memcpy(inner, s + 2, n);
                inner[n] = '\0';
                emit(out, inner, data);
                free(inner);
                s = e + 1;
                continue;
            }
        }
        sb_putc(out, *s++);
    }
}

/* Perl's s/R<(\w+?)>/.../g : word characters only between the brackets */
static void append_r_tag(SB *out, const char *s, EmitFn emit, void *data)
{
    for ( ; *s; ) {
        if (s[0] == 'R' && s[1] == '<') {
            const char *e = s + 2;
            while (isalnum((unsigned char)*e) || *e == '_') e++;
            if (e > s + 2 && *e == '>') {
                size_t n = (size_t)(e - s - 2);
                char *inner = xmalloc(n + 1);
                memcpy(inner, s + 2, n);
                inner[n] = '\0';
                emit(out, inner, data);
                free(inner);
                s = e + 1;
                continue;
            }
        }
        sb_putc(out, *s++);
    }
}

/* ^\s*=item\s+ -> @item , anchored at the start of the line and only there,
 * because the Perl pattern has no /m */
static void append_item(SB *out, const char *s, const char *repl)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (strncmp(p, "=item", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        sb_puts(out, repl);
        sb_puts(out, p);
        return;
    }
    sb_puts(out, s);
}

static void emit_xref_display(SB *out, const char *inner, void *data)
{
    (void)data;
    sb_puts(out, "@xref{");
    sb_puts(out, display_of(inner));
    sb_putc(out, '}');
}

static void emit_code(SB *out, const char *inner, void *data) { (void)data; sb_puts(out, "@code{"); sb_puts(out, inner); sb_putc(out, '}'); }
/* the Perl writes "\@b{$1}>" and "\@emph{$1}>" with the closing > of the
 * pattern left behind; the three places where the sources use B<> or I<>
 * (B<empty>, B<after>, I<root>) come out as "@b{empty}>", "@b{after}>" and
 * "@emph{root}>" there and as "@b{empty}", "@b{after}" and "@emph{root}" here */
static void emit_bold(SB *out, const char *inner, void *data) { (void)data; sb_puts(out, "@b{");    sb_puts(out, inner); sb_putc(out, '}'); }
static void emit_emph(SB *out, const char *inner, void *data) { (void)data; sb_puts(out, "@emph{"); sb_puts(out, inner); sb_putc(out, '}'); }

static void emit_html_tt(SB *out, const char *inner, void *data) { (void)data; sb_puts(out, "\001tt>"); sb_puts(out, inner); sb_puts(out, "\001/tt>"); }
static void emit_html_b(SB *out, const char *inner, void *data)  { (void)data; sb_puts(out, "\001B>");  sb_puts(out, inner); sb_puts(out, "\001/B>"); }
static void emit_html_i(SB *out, const char *inner, void *data)  { (void)data; sb_puts(out, "\001I>");  sb_puts(out, inner); sb_puts(out, "\001/I>"); }

static void emit_html_r(SB *out, const char *inner, void *data)
{
    (void)data;
    sb_puts(out, "\001a href=\"#command_");
    sb_puts(out, inner);
    sb_puts(out, "\">");
    sb_puts(out, display_of(inner));
    sb_puts(out, "\001/a>");
}

/* the text of one line of a command for the texinfo output */
static char *texi_line(const char *line)
{
    static const char *const from[] = { "<tt>", "</tt>", "<UL>", "</UL>", "<LI>" };
    static const char *const to[]   = { "@code{", "}", "@itemize ", "@end itemize ", "@item " };
    char *s = replace_list_ci(line, from, to, 5);

    SB a; sb_init(&a); append_escaped(&a, s);                     free(s); s = sb_take(&a);
    SB b; sb_init(&b); append_anchors(&b, s);                     free(s); s = sb_take(&b);
    SB c; sb_init(&c); append_r_tag(&c, s, emit_xref_display, NULL); free(s); s = sb_take(&c);
    SB d; sb_init(&d); append_tag(&d, s, 'T', emit_code, NULL);   free(s); s = sb_take(&d);
    SB e; sb_init(&e); append_tag(&e, s, 'B', emit_bold, NULL);   free(s); s = sb_take(&e);
    SB f; sb_init(&f); append_tag(&f, s, 'I', emit_emph, NULL);   free(s); s = sb_take(&f);
    SB g; sb_init(&g); append_item(&g, s, "@item ");              free(s); s = sb_take(&g);
    return s;
}

/* the text of one line of a command for the HTML output.  The \001 marks are
 * the placeholders of the Perl program, turned into < at the end. */
static char *html_line(const char *line)
{
    SB a; sb_init(&a); append_r_tag(&a, line, emit_html_r, NULL);
    char *s = sb_take(&a);
    SB b; sb_init(&b); append_tag(&b, s, 'T', emit_html_tt, NULL); free(s); s = sb_take(&b);
    SB c; sb_init(&c); append_tag(&c, s, 'B', emit_html_b, NULL);  free(s); s = sb_take(&c);
    SB d; sb_init(&d); append_tag(&d, s, 'I', emit_html_i, NULL);  free(s); s = sb_take(&d);
    SB e; sb_init(&e); append_item(&e, s, "\001LI>");              free(s); s = sb_take(&e);
    for (char *p = s; *p; p++) if (*p == '\001') *p = '<';
    return s;
}

/* ------------------------------------------------------------- the outputs */

static void write_texi(const char *path)
{
    FILE *F = fopen(path, "wb");
    if (!F) { fprintf(stderr, "Can not open %s.\n", path); exit(1); }

    fputs("@chapter Command reference\n", F);

    int count;
    char **names = sorted_commands(&count);
    for (int i = 0; i < count; i++) {
        Command *c = get_command(names[i]);
        const char *title = c->title ? c->title : c->name;
        fprintf(F, "\n@section %s\n\n", title);
        if (c->subtitle) fprintf(F, "@b{%s}\n", c->subtitle);
        int verbatim = 0;
        for (int j = 0; j < c->nline; j++) {
            const char *line = c->line[j];
            if (line_is(line, "=details")) { fprintf(F, "@subsection %s Details\n", c->name); continue; }
            if (line_is(line, "=verbatim")) { fputs("@example\n", F); verbatim = 1; continue; }
            if (line_is(line, "=noverbatim")) { fputs("@end example\n", F); verbatim = 0; continue; }
            if (verbatim) { fputs(line, F); continue; }
            if (line_is(line, "=itemize")) { fputs("@itemize\n", F); continue; }
            if (line_is(line, "=noitemize")) { fputs("@end itemize\n", F); continue; }
            if (is_blank(line)) { fputs("\n\n", F); continue; }
            char *t = texi_line(line);
            fputs(t, F);
            free(t);
        }
    }
    free(names);
    fclose(F);
}

static void mkdir_path(const char *path, mode_t mode)
{
    mkdir(path, mode);              /* it may be there already, as in the Perl */
}

static void write_html(const char *dir)
{
    SB path; sb_init(&path);

    mkdir_path(dir, 0777);
    sb_puts(&path, dir); sb_puts(&path, "/commands.html");
    FILE *F = fopen(sb_str(&path), "wb");
    if (!F) { fprintf(stderr, "Can not open %s.\n", sb_str(&path)); exit(1); }

    fputs("<HTML>\n<HEAD>\n<TITLE></TITLE>\n</HEAD>\n<BODY>\n"
          "<H1>ScriptBasic commands and functions reference</H1>\n\n"
          "<H2>List of Sections</H2>\n", F);

    int nsection;
    char **sections = sorted_sections(&nsection);
    for (int i = 0; i < nsection; i++)
        fprintf(F, "<a href=\"#section_%s\">%s</A>\n", sections[i], sections[i]);

    fputs("<H2>List of Commands</H2>\n<FONT SIZE=\"1\">\n", F);

    int nname;
    char **names = sorted_commands(&nname);
    for (int i = 0; i < nname; i++)
        fprintf(F, "<a href=\"#command_%s\">%s</A>\n", names[i], display_of(names[i]));

    fputs("</FONT>\n<H2>List of Commands by Sections</H2>\n", F);

    for (int i = 0; i < nsection; i++) {
        fprintf(F, "<H3><a name=\"section_%s\">%s</A></H3>\n", sections[i], sections[i]);
        fputs("<FONT SIZE=\"1\">\n", F);
        Section *s = NULL;
        for (int k = 0; k < nsect; k++) if (strcmp(sect[k].name, sections[i]) == 0) s = &sect[k];
        if (s) {
            char **v = xmalloc((size_t)s->ncommand * sizeof *v);
            for (int k = 0; k < s->ncommand; k++) v[k] = s->command[k];
            qsort(v, (size_t)s->ncommand, sizeof *v, cmp_str);
            for (int k = 0; k < s->ncommand; k++)
                fprintf(F, "<a href=\"#command_%s\">%s</A>\n", v[k], display_of(v[k]));
            free(v);
        }
        fputs("</FONT>\n", F);
    }

    fputs("<H2>Commands</H2>\n", F);

    for (int i = 0; i < nname; i++) {
        Command *c = get_command(names[i]);
        const char *title = c->title ? c->title : c->name;
        FILE *DF = NULL;
        int verbatim = 0;

        fprintf(F, "<H3><a name=\"command_%s\">%s</A></H3>\n", c->name, title);
        if (c->subtitle) fprintf(F, "<H4>%s</H4>\n", c->subtitle);
        fputs("<BLOCKQUOTE>\n", F);

        for (int j = 0; j < c->nline; j++) {
            const char *line = c->line[j];
            if (line_is(line, "=details")) {
                fprintf(F, "<a href=\"commands/%s.html\">details</A>\n", c->name);
                SB cdir; sb_init(&cdir);
                sb_puts(&cdir, dir); sb_puts(&cdir, "/commands");
                mkdir_path(sb_str(&cdir), 0777);
                SB cfile; sb_init(&cfile);
                sb_puts(&cfile, sb_str(&cdir)); sb_puts(&cfile, "/"); sb_puts(&cfile, c->name); sb_puts(&cfile, ".html");
                DF = fopen(sb_str(&cfile), "wb");
                if (!DF) { fprintf(stderr, "Can not open %s\n", sb_str(&cfile)); exit(1); }
                sb_free(&cfile); sb_free(&cdir);
                fprintf(DF, "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n"
                            "<H1>%s</H1>\n"
                            "<a href=\"../commands.html#command_%s\">BACK</a><P>\n",
                        c->name, title, c->name);
                continue;
            }
            if (line_is(line, "=verbatim")) { fputs("<PRE>\n", DF ? DF : F); verbatim = 1; continue; }
            if (line_is(line, "=noverbatim")) { fputs("</PRE>\n", DF ? DF : F); verbatim = 0; continue; }
            if (verbatim) { fputs(line, DF ? DF : F); continue; }
            if (line_is(line, "=itemize")) { fputs("<UL>\n", DF ? DF : F); continue; }
            if (line_is(line, "=noitemize")) { fputs("</UL>\n", DF ? DF : F); continue; }
            if (is_blank(line)) { fputs("<P>\n", DF ? DF : F); continue; }
            char *h = html_line(line);
            fputs(h, DF ? DF : F);
            free(h);
        }

        fputs("</BLOCKQUOTE>\n", F);
        if (DF) {
            fprintf(DF, "<P><a href=\"../commands.html#command_%s\">BACK</a>\n</BODY>\n</HTML>\n", c->name);
            fclose(DF);
        }
    }

    fputs("</BODY>\n</HTML>\n", F);
    fclose(F);
    sb_free(&path);
    free(names);
    free(sections);
}

int main(int argc, char **argv)
{
    const char *commands = "src/commands";
    const char *texifile = "commands.texi";
    const char *htmldir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--commands") == 0 && i + 1 < argc) { commands = argv[++i]; continue; }
        if (strcmp(argv[i], "--texi") == 0 && i + 1 < argc) { texifile = argv[++i]; continue; }
        if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) { htmldir = argv[++i]; continue; }
        fprintf(stderr, "usage: mkcdoc [--commands <dir>] [--texi <file>] [--html <dir>]\n");
        return 1;
    }

    DIR *d = opendir(commands);
    if (!d) { fprintf(stderr, "Can not open directory %s.\n", commands); return 1; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        SB p; sb_init(&p);
        sb_puts(&p, commands); sb_puts(&p, "/"); sb_puts(&p, e->d_name);
        process_file(sb_str(&p));
        sb_free(&p);
    }
    closedir(d);

    write_texi(texifile);
    if (htmldir) write_html(htmldir);
    return 0;
}
