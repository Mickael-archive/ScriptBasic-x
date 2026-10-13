/*
 * ssplit: the source splitter, the C port of ssplit.pl.
 *
 * A text file is split into several files by markers that sit on lines of
 * their own:
 *
 *     %file <name>      the lines that follow go to the file <name>
 *     %dir  <subdir>    the directory the names of the next %file lines are
 *                       relative to
 *
 * The tool was written to keep many small HTML files as one somewhat larger
 * jamal source, compile that with jamal and split the result back into the
 * small files.
 *
 * The port follows the Perl step by step:
 *
 *  * a file whose content has not changed is not written at all, which is what
 *    `return if $buffer eq $sbuffer' does in the Perl;
 *  * the markers are the regular expressions /^\s*%\s*file\s+(.*?)\s*$/ and
 *    /^\s*%\s*dir\s+(.*?)\s*$/, so the name is the rest of the line without the
 *    spaces around it;
 *  * `$output =~ s{//}{/}' takes the first doubled slash out and only the first
 *    one (there is no /g in the Perl): with the default directory `.` and no
 *    %dir, ".//name" becomes "./name" and "dir//name" becomes "dir/name";
 *  * text before the first `%file' line is an error;
 *  * the directory of a file is made one level at a time, from the first
 *    component of its name on.
 *
 * One thing is not the Perl: the Perl reads its input in text mode, so on
 * Windows the CR of a CRLF source goes off when the line is read and on UNIX it
 * stays in the text.  The CR goes off with the LF here, on either system, the
 * way the other ports of the tree do it, so the files written have the line
 * ending of the source with the CR taken off.
 */

#define _POSIX_C_SOURCE 200809L   /* getline */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *version = "1.0";

/* the text of the file that is being built */
static char  *buffer = NULL;
static size_t buffer_len = 0, buffer_cap = 0;

/* the name of the file that is being built; "" while no %file was seen yet */
static char  *output = NULL;

/* $subdir; NULL stands for the empty string of the Perl */
static char  *subdir = NULL;

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* $buffer = ''; */
static void buffer_reset(void)
{
    buffer_len = 0;
    if (buffer) buffer[0] = '\0';
}

/* $buffer .= $text; */
static void buffer_append(const char *text, size_t n)
{
    if (buffer_len + n + 1 > buffer_cap) {
        buffer_cap = (buffer_len + n + 1) * 2 + 16;
        buffer = realloc(buffer, buffer_cap);
        if (!buffer) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memcpy(buffer + buffer_len, text, n);
    buffer_len += n;
    buffer[buffer_len] = '\0';
}

/* mkdir $path, 0777 unless -d $path; */
static void mkdir_one(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
    mkdir(path, 0777);
}

/*
 * sub make_dir: my @dlist = split '/' , $dir; pop @dlist; return if $#dlist ==
 * -1; $root = ''; for (@dlist) { $root .= '/' if $root; $root .= $_;
 * mkdir $root, 0777 unless -d $root }
 */
static void make_dir(const char *path)
{
    size_t n = 0, i;
    char **part = NULL;
    char *root;
    const char *p, *q;

    /* split on '/'; Perl's split drops the empty fields at the end */
    for (p = path; ; ) {
        size_t l;
        q = strchr(p, '/');
        if (!q) {
            if (*p) {                            /* the last field, if it is not empty */
                part = realloc(part, (n + 1) * sizeof *part);
                part[n++] = xstrdup(p);
            }
            break;
        }
        l = (size_t)(q - p);                     /* an empty field in the middle stays */
        part = realloc(part, (n + 1) * sizeof *part);
        {
            char *s = xmalloc(l + 1);
            memcpy(s, p, l); s[l] = '\0';
            part[n++] = s;
        }
        p = q + 1;
    }

    if (n == 0) { free(part); return; }
    n--;                                        /* pop the file name */

    root = xstrdup("");
    for (i = 0; i < n; i++) {
        char *r;
        if (*root) {
            r = xmalloc(strlen(root) + 2);
            strcpy(r, root); strcat(r, "/");
            free(root); root = r;
        }
        r = xmalloc(strlen(root) + strlen(part[i]) + 1);
        strcpy(r, root); strcat(r, part[i]);
        free(root); root = r;
        mkdir_one(root);
    }
    free(root);
    for (i = 0; i < n; i++) free(part[i]);
    free(part);
}

static void die(const char *text, const char *name, int status)
{
    fprintf(stderr, text, name);
    exit(status);
}

/*
 * sub save_buffer: return unless $output || $buffer; if ($buffer && !$output)
 * die ...; return if the file already holds exactly this text; make_dir;
 * open and write.
 */
static void save_buffer(void)
{
    FILE *F;
    int have_output = (output && *output);

    if (!have_output && buffer_len == 0) return;
    if (buffer_len && !have_output) {
        fprintf(stderr, "Is there text before the first '%%file' ??\n");
        exit(255);
    }

    F = fopen(output, "rb");
    if (F) {
        long size;
        char *old;
        fseek(F, 0, SEEK_END);
        size = ftell(F);
        fseek(F, 0, SEEK_SET);
        old = xmalloc((size_t)(size > 0 ? size : 0) + 1);
        if (size > 0 && fread(old, 1, (size_t)size, F) != (size_t)size) size = 0;
        old[size > 0 ? size : 0] = '\0';
        fclose(F);
        if ((size_t)(size > 0 ? size : 0) == buffer_len &&
            memcmp(old, buffer, buffer_len) == 0) {
            free(old);
            return;
        }
        free(old);
    }

    make_dir(output);
    F = fopen(output, "wb");
    if (!F) die("Can not output %s\n", output, errno ? errno : 1);
    if (buffer_len) fwrite(buffer, 1, buffer_len, F);
    fclose(F);
}

int main(int argc, char **argv)
{
    const char *file, *dir;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t got;
    FILE *F;

    if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        printf("Source Splitter %s\n"
               "Usage:\n"
               "\n"
               "   ssplit source_file [directory]\n"
               "\n"
               "This program splits a text file. The primary\n"
               "purpose of this tool was to maintain many small HTML files as a one\n"
               "somewhat larger jamal source, compile it using jamal and then split\n"
               "to small html files. The source file should contain lines\n"
               "\n"
               "%%file file_name\n"
               "\n"
               "to start a new file. The default directory is .\n"
               "\n"
               "For further information see on-line documentation at\n"
               "\n"
               "            http://www.isys.hu/c/verhas/progs/perl/ssplit\n",
               version);
        return 0;
    }

    file = (argc > 1) ? argv[1] : "";
    dir  = (argc > 2 && argv[2][0]) ? argv[2] : ".";

    output = xstrdup("");

    F = fopen(file, "rb");
    if (!F) die("Can not open %s\n", file, errno ? errno : 1);

    while ((got = getline(&line, &line_cap, F)) >= 0) {
        size_t len = (size_t)got;
        int has_lf = (len > 0 && line[len - 1] == '\n');
        size_t body = has_lf ? len - 1 : len;      /* the line without the LF */
        const char *p;
        int is_file = 0, is_dir = 0;
        char *value = NULL;

        if (body > 0 && line[body - 1] == '\r') body--;   /* and without the CR */

        /* /^\s*%\s*file\s+(.*?)\s*$/ and the same with dir */
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '%') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "file", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
                is_file = 1; p += 4;
            } else if (strncmp(p, "dir", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) {
                is_dir = 1; p += 3;
            }
            if (is_file || is_dir) {
                size_t start, end;
                while (*p == ' ' || *p == '\t') p++;
                start = (size_t)(p - line);
                end = body;
                while (end > start &&
                       (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
                value = xmalloc(end - start + 1);
                memcpy(value, line + start, end - start);
                value[end - start] = '\0';
            }
        }

        if (is_file) {
            char *name, *slash;
            size_t n;
            save_buffer();
            buffer_reset();
            n = strlen(dir) + (subdir ? strlen(subdir) : 0) + strlen(value) + 3;
            name = xmalloc(n);
            sprintf(name, "%s/%s/%s", dir, subdir ? subdir : "", value);
            free(value);
            slash = strstr(name, "//");        /* s{//}{/} on the first one */
            if (slash) memmove(slash + 1, slash + 2, strlen(slash + 2) + 1);
            free(output);
            output = name;
            free(line); line = NULL; line_cap = 0;
            continue;
        }
        if (is_dir) {
            free(subdir);
            subdir = value;                    /* $subdir = $1; */
            free(line); line = NULL; line_cap = 0;
            continue;
        }
        free(value);

        buffer_append(line, body);
        if (has_lf) buffer_append("\n", 1);
        free(line); line = NULL; line_cap = 0;
    }
    free(line);
    fclose(F);

    save_buffer();
    return 0;
}
