/*
 * setup.c -- C23 replacement for setup.pl
 *
 * The build driver of ScriptBasic.  It configures the source tree, builds the
 * C helper tools, runs them, asks the Jamal preprocessor to turn the *.jam
 * sources into makefiles, configures every module under extensions/, writes the
 * shell command file that performs the compilation, and optionally installs
 * (--install) or reports (--check) the result.
 *
 * This program is a port of setup.pl: the messages, the generated files and the
 * exit statuses match the Perl original.  The data it carries inline in the
 * original - the option help texts, the module skeleton files and the shell and
 * configuration templates - are in setup_data.h / setup_text.h, extracted from
 * setup.pl byte for byte by tools/codegen/extract_setup_data.py.
 *
 * Places where the original relies on a Perl hash iteration order (which Perl
 * randomises per process) are iterated in table order here, which makes the
 * result reproducible:
 *
 *   - %OPT when an invalid option is reported (the Perl message names an
 *     arbitrary one of the offending options),
 *   - keys %HELP when the interactive install questions are asked,
 *   - the %depheaders set of create_makefile_jam (the dependency lists of a
 *     generated makefile.jam).
 *
 * The esd.pm check is a search over the directories a Perl @INC would offer
 * (PERL5LIB plus the usual system directories), keeping the original rule that
 * entries with a dot in them are ignored.  A C program cannot ask Perl which
 * modules it can see, and the module is only needed by the documentation
 * chain, which still runs through the Perl Jamal.
 *
 * Usage: the same options as setup.pl, see setup --help.
 */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "setup_data.h"
#include "setup_text.h"

/* ------------------------------------------------------------------ memory */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("Out of memory\n", stderr); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("Out of memory\n", stderr); exit(1); }
    return q;
}

static char *xstrdup(const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static char *xmemdup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ----------------------------------------------------------- string buffer */

typedef struct {
    char *p;
    size_t len, cap;
} SB;

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

static void sb_printf(SB *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { sb_puts(b, tmp); return; }
    char *big = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_puts(b, big);
    free(big);
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

/* Perl's \s: space, tab, newline, carriage return, form feed, vertical tab */
static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* ------------------------------------------------------------ file helpers */

static int file_exists(const char *path)      /* Perl -e */
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_dir(const char *path)           /* Perl -d */
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


/* Perl's mkdir makes one level and reports failure through $! - the original
 * ignores the result, so failures are ignored here too. */
static void mkdir_mode(const char *path, mode_t mode)
{
    if (mkdir(path, mode) != 0) { /* deliberately ignored, as in the original */ }
}

static void chmod_path(const char *path, mode_t mode)
{
    chmod(path, mode);
}

static void unlink_path(const char *path)
{
    unlink(path);
}


/* the first line of a file, without the line ending (Perl's <F> + chomp) */

static int write_file_mode(const char *path, const char *content, mode_t mode)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (content) fwrite(content, 1, strlen(content), f);
    fclose(f);
    if (mode) chmod(path, mode);
    return 1;
}

/* list a directory the way Perl's readdir does: in filesystem order, with the
 * '.' and '..' entries included. */
static char **list_dir(const char *path, int *count)
{
    DIR *d = opendir(path);
    *count = 0;
    if (!d) return NULL;
    int cap = 32, n = 0;
    char **v = xmalloc((size_t)cap * sizeof *v);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (n == cap) { cap *= 2; v = xrealloc(v, (size_t)cap * sizeof *v); }
        v[n++] = xstrdup(e->d_name);
    }
    closedir(d);
    v = xrealloc(v, (size_t)(n ? n : 1) * sizeof *v);
    *count = n;
    return v;
}

static void free_list(char **v, int n)
{
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

/* ------------------------------------------------------------ run commands */
/* Perl's backticks: run through the shell, capture stdout.  The original
 * discards the captured text in most places (run_quiet) and prints it in a few
 * others (run_capture).
 *
 * Perl only starts a shell when the command needs one.  A command built from
 * plain words is passed to execvp directly, and then no shell is there to print
 * a "Segmentation fault" line for a child that dies - which happens on purpose
 * when --install probes the safe maxlevel value with an endless recursion.  The
 * same check is done here so that the messages agree. */

/* the characters that make a command line a job for the shell, as in Perl */
static int needs_shell(const char *cmd)
{
    for (const char *p = cmd; *p; p++) {
        if (strchr("\t\n~`<>(){}[]?*$#;&|\\\"'", *p)) return 1;
    }
    return 0;
}

#ifndef _WIN32
static int run_direct(const char *cmd, char *const *envp)
{
    (void)envp;
    /* split on spaces, the way the generated command lines are built */
    char *copy = xstrdup(cmd);
    char *argv[64];
    int argc = 0;
    for (char *w = strtok(copy, " \t"); w && argc < 63; w = strtok(NULL, " \t"))
        argv[argc++] = w;
    argv[argc] = NULL;
    if (argc == 0) { free(copy); return -1; }

    pid_t pid = fork();
    if (pid < 0) { free(copy); return -1; }
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); if (fd != STDOUT_FILENO) close(fd); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    free(copy);
    return status;
}
#endif

static int run_quiet(const char *cmd)
{
#ifndef _WIN32
    if (!needs_shell(cmd)) return run_direct(cmd, NULL);
#endif
    SB b; sb_init(&b);
    sb_puts(&b, cmd);
#ifdef _WIN32
    sb_puts(&b, " > NUL");
#else
    sb_puts(&b, " > /dev/null");
#endif
    int rc = system(b.p);
    sb_free(&b);
    if (rc == -1) return -1;
    return rc;
}

static char *run_capture(const char *cmd)
{
    SB out; sb_init(&out);
#ifdef _WIN32
    FILE *p = _popen(cmd, "r");
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p) return sb_detach(&out);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) {
        sb_need(&out, n);
        memcpy(out.p + out.len, buf, n);
        out.len += n;
        out.p[out.len] = '\0';
    }
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return sb_detach(&out);
}

/* ------------------------------------------------------------------ options */

typedef struct { char *name; char *value; } Option;

static Option Opt[400];
static int    nOpt = 0;

static char *optv(const char *name)            /* $OPT{name}, NULL if undef */
{
    for (int i = 0; i < nOpt; i++)
        if (strcmp(Opt[i].name, name) == 0) return Opt[i].value;
    return NULL;
}

/* Perl truthiness of $OPT{name} */
static int opt_true(const char *name)
{
    const char *v = optv(name);
    return v && *v && strcmp(v, "0") != 0;
}

static void opt_set(const char *name, const char *value)
{
    for (int i = 0; i < nOpt; i++) {
        if (strcmp(Opt[i].name, name) == 0) {
            free(Opt[i].value);
            Opt[i].value = xstrdup(value);
            return;
        }
    }
    Opt[nOpt].name  = xstrdup(name);
    Opt[nOpt].value = xstrdup(value);
    nOpt++;
}

/* Perl's negopt(): --no-x <-> --x */
static char *negopt(const char *opt)
{
    SB b; sb_init(&b);
    if (strncmp(opt, "--no-", 5) == 0) { sb_puts(&b, "--"); sb_puts(&b, opt + 5); }
    else { sb_puts(&b, "--no-"); sb_puts(&b, opt + 2); }
    return sb_detach(&b);
}

static const char *help_for(const char *option)
{
    for (int i = 0; i < SETUP_HELP_COUNT; i++)
        if (strcmp(SETUP_HELP[i].name, option) == 0) return SETUP_HELP[i].text;
    return NULL;
}

static const char *install_default_for(const char *option)
{
    for (int i = 0; i < SETUP_INSTALL_DEFAULT_COUNT; i++)
        if (strcmp(SETUP_INSTALL_DEFAULT[i].name, option) == 0) return SETUP_INSTALL_DEFAULT[i].text;
    return NULL;
}

/* ------------------------------------------------------------------- log it */

static FILE *LogFile = NULL;

static void logit(const char *msg)
{
    if (!LogFile) {
        LogFile = fopen("setup.log", opt_true("--log-append") ? "a" : "w");
        if (!LogFile) { fputs("Can not open log file.\n", stderr); exit(1); }
    }
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *lt = localtime_r(&t, &tmv);
    /* Perl: sprintf("%04s.%02s...") pads strings with SPACES, so the values
     * (which are two or four digits anyway) are printed as they are. */
    fprintf(LogFile, "%04d.%02d.%02d %02d:%02d:%02d %s\n",
            lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
            lt->tm_hour, lt->tm_min, lt->tm_sec, msg);
    fflush(LogFile);
}

/* -------------------------------------------------------- interpolation ---- */
/* Perl interpolates its heredocs: $NAME and ${NAME} are replaced by the value
 * of the variable, \$ stands for a literal dollar.  A backslash also escapes
 * every other character: the escape sequences Perl knows are translated, any
 * other \X stands for the character X itself, so \@ is an at sign and a lone
 * backslash in front of a path separator disappears, exactly as the Perl
 * heredocs behave.  The same table and the same rules are used here for the
 * templates in setup_text.h. */

typedef struct { char *name; char *value; } Subst;

static Subst Sub[40];
static int   nSub = 0;

static void subst_set(const char *name, const char *value)
{
    for (int i = 0; i < nSub; i++) {
        if (strcmp(Sub[i].name, name) == 0) { free(Sub[i].value); Sub[i].value = xstrdup(value); return; }
    }
    Sub[nSub].name  = xstrdup(name);
    Sub[nSub].value = xstrdup(value ? value : "");
    nSub++;
}

static const char *subst_get(const char *name)
{
    for (int i = 0; i < nSub; i++)
        if (strcmp(Sub[i].name, name) == 0) return Sub[i].value;
    return NULL;
}

static char *interp(const char *text)
{
    SB b; sb_init(&b);
    const char *p = text;
    while (*p) {
        if (p[0] == '\\' && p[1] == '$') { sb_putc(&b, '$'); p += 2; continue; }

        if (p[0] == '\\' && p[1]) {         /* the Perl double quoted escapes */
            switch (p[1]) {
            case 'n':  sb_putc(&b, '\n'); break;
            case 't':  sb_putc(&b, '\t'); break;
            case 'r':  sb_putc(&b, '\r'); break;
            case 'f':  sb_putc(&b, '\f'); break;
            case 'b':  sb_putc(&b, '\b'); break;
            case 'a':  sb_putc(&b, '\a'); break;
            case 'e':  sb_putc(&b, 27);    break;
            case 'x': {                      /* \xHH */
                int v = 0, k = 0;
                const char *q = p + 2;
                while (k < 2 && isxdigit((unsigned char)*q)) {
                    v = v * 16 + (isdigit((unsigned char)*q) ? *q - '0'
                                                             : (tolower((unsigned char)*q) - 'a' + 10));
                    q++; k++;
                }
                if (k) { sb_putc(&b, (char)v); p = q; continue; }
                sb_putc(&b, 'x');
                break;
            }
            default:  sb_putc(&b, p[1]); break;   /* \\, \@, \*, ... */
            }
            p += 2;
            continue;
        }

        if (p[0] == '$' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
            /* the longest known name that matches wins, so that $tofils is not
             * cut short at $to */
            const char *best = NULL;
            size_t bestlen = 0;
            for (int i = 0; i < nSub; i++) {
                size_t nl = strlen(Sub[i].name);
                if (nl <= bestlen) continue;
                if (strncmp(p + 1, Sub[i].name, nl) != 0) continue;
                char after = p[1 + nl];
                if (isalnum((unsigned char)after) || after == '_') continue;
                best = Sub[i].value;
                bestlen = nl;
            }
            if (best) { sb_puts(&b, best); p += 1 + bestlen; continue; }
            /* an undefined variable interpolates as the empty string */
            p++;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            continue;
        }

        if (p[0] == '$' && p[1] == '{') {
            const char *e = strchr(p + 2, '}');
            if (e && (size_t)(e - p - 2) < 63) {
                char name[64];
                size_t n = (size_t)(e - p - 2);
                memcpy(name, p + 2, n);
                name[n] = '\0';
                const char *v = subst_get(name);
                if (v) sb_puts(&b, v);
                p = e + 1;
                continue;
            }
        }

        sb_putc(&b, *p++);
    }
    return sb_detach(&b);
}

/* ---------------------------------------------------------------- helpers -- */

static void fputs_interp(const char *text, FILE *f)
{
    char *s = interp(text);
    fputs(s, f);
    free(s);
}

static char *strf(const char *fmt, ...)
{
    SB b; sb_init(&b);
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    sb_puts(&b, tmp);
    return sb_detach(&b);
}


/* -------------------------------------------------------- forward decls --- */

static void check_compilation_result(void);
static void save_options(void);
static void pick_jamal(void);
static void configure_module(const char *module);
static void create_makefile_jam(const char *module);
static void create_module_skeleton(const char *module);
static void create_skeleton_file(const char *module, const char *file);
static void install_unix_scriba(void);
static void help_main(void);
static void self_check(void);
static void get_config_file(void);
static char *ius_CreateDirectory(const char *optname, const char *permission);
static char *ius_CopyFile(const char *file, const char *to, const char *owner, const char *permission);
static char *ius_CopyFiles(const char *file, const char *destination, const char *tofils,
                           const char *owner, const char *permission);

/* -------------------------------------------------------------- globals ---- */

static char  *cwd = NULL;            /* source root, with a trailing separator */
static char  *ds = NULL;             /* directory separator */
static const char *unixName = NULL;  /* "" | "unix" | "darwin" */
static const char *build = "nt";     /* "unix" | "darwin" | "nt" */
static mode_t mask_mode = 0700;
static const char *jamalTool = NULL; /* the Jamal preprocessor to run */
static int    lastOptionWasHelp = 0;

/* the installation variables, as the Perl globals */
static char *CONFIGDIR, *INCLUDE, *SOURCE, *MODULE, *DOCU, *LIB, *CACHE,
            *HEBTEMP, *BIN, *LOG, *ETC;
static int   MAXLEVEL = 0;

/* ---------------------------------------------------------------- printing - */

/* Perl's die: the message goes to stderr and the program stops. */


/* --------------------------------------------------- program entry point --- */

/* the templates of setup_text.h, by the line they come from in setup.pl */
#define HD_SETUP_INTRO       SETUP_HD[0]
#define HD_ESD_MISSING       SETUP_HD[1]
#define HD_ZIP_DONE          SETUP_HD[2]
#define HD_INSTALL_UNIX      SETUP_HD[3]
#define HD_INSTALL_NT        SETUP_HD[4]
#define HD_CORE_NOTE         SETUP_HD[5]
#define HD_MAKEFILE_JAM      SETUP_HD[6]
#define HD_IUS_DIR           SETUP_HD[7]
#define HD_IUS_COPYFILE      SETUP_HD[8]
#define HD_IUS_COPYFILES     SETUP_HD[9]
#define HD_INSTALL_SH_HEAD   SETUP_HD[10]
#define HD_INSTALL_SH_CACHE  SETUP_HD[11]
#define HD_INSTALL_SH_CONF   SETUP_HD[12]
#define HD_INSTALL_SH_TAIL   SETUP_HD[13]
#define HD_INITD_SBHTTPD     SETUP_HD[14]
#define HD_CONFIG_PL         SETUP_HD[15]
#define HD_MKDEB_SH          SETUP_HD[16]
#define HD_MKRPM_SH          SETUP_HD[17]
#define HD_HELP_INVALID      SETUP_HD[18]
#define HD_HELP_USAGE        SETUP_HD[19]
#define HD_HELP_TAIL         SETUP_HD[20]
#define HD_SCRIBA_CONF       SETUP_HD[21]

/* Perl's `die "...";` appends " at <script> line <n>." and exits with the
 * current errno when there is one. */
static void die_at(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, " at setup.pl line %d.\n", line);
    exit(errno ? errno : 255);
}

/* Perl's `die "...\n";` only prints the message. */
static void die_plain(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(255);
}

static void print_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

/* --------------------------------------------------------------- arguments - */

static void parse_options(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* ($optn,$optv) = split /=/ , $opt;  -- at most the first two fields */
        SB name; sb_init(&name);
        const char *eq = strchr(arg, '=');
        if (eq) {
            sb_need(&name, (size_t)(eq - arg));
            memcpy(name.p, arg, (size_t)(eq - arg));
            name.len = (size_t)(eq - arg);
            name.p[name.len] = '\0';
        } else {
            sb_puts(&name, arg);
        }
        const char *value = NULL;
        char *valbuf = NULL;
        if (eq) {
            const char *vstart = eq + 1;
            const char *vend = strchr(vstart, '=');
            valbuf = vend ? xmemdup(vstart, (size_t)(vend - vstart)) : xstrdup(vstart);
            value = valbuf;
        }

        char *neg = negopt(name.p);
        if (optv(neg) != NULL) {
            /* $nopt is never assigned in the original: the message shows a
             * double space where its value would be */
            fprintf(stderr, "Can not use option %s and  at a time"
                            " at setup.pl line 64.\n", name.p);
            exit(255);
        }
        free(neg);

        if (!value || !*value) value = "1";
        opt_set(name.p, value);
        lastOptionWasHelp = strcmp(name.p, "--help") == 0;
        free(valbuf);
        sb_free(&name);
    }
}

static void validate_options(void)
{
    for (int i = 0; i < nOpt; i++) {
        const char *name = Opt[i].name;
        if (strcmp(name, "--help") == 0) continue;
        if (help_for(name)) continue;
        char *neg = negopt(name);
        const char *h = help_for(neg);
        free(neg);
        if (h) continue;
        fprintf(stderr, "Invalid option: %s\n", name);
        exit(255);
    }
}

static void load_options_file(void)
{
    const char *fname = optv("--load");
    if (!fname) return;
    if (strcmp(fname, "1") == 0) {
        opt_set("--load", "configure.save");
        fname = optv("--load");
    }
    FILE *f = fopen(fname, "rb");
    if (!f) die_at(88, "Can not open configuration file %s", fname);

    char line[4096];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;                 /* comment */
        if (!*p) continue;                       /* empty */
        const char *eq = strchr(line, '=');
        char *name = eq ? xmemdup(line, (size_t)(eq - line)) : xstrdup(line);
        const char *value = eq ? eq + 1 : "";
        if (optv(name) == NULL) {
            char *neg = negopt(name);
            if (optv(neg) == NULL) opt_set(name, value);
            free(neg);
        }
        free(name);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- the OS --- */

static void determine_os(void)
{
    if (optv("--nt") && *optv("--nt")) {
        unixName = "";
        ds = "\\";
    } else if (optv("--unix") && *optv("--unix")) {
        unixName = "unix";
        ds = "/";
    } else if (optv("--darwin") && *optv("--darwin")) {
        unixName = "darwin";
        ds = "/";
    } else {
        /* try to figure it out: `pwd` works under UNIX, `CD` under NT */
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof buf)) {
            unixName = "unix";
            struct utsname u;
            if (uname(&u) == 0 && strcmp(u.sysname, "Darwin") == 0) unixName = "darwin";
            ds = "/";
        } else {
            die_at(141, "Out of luck determining the OS, nor 'pwd' neither 'CD' worked.");
        }
    }
}

static void set_cwd(void)
{
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) {
        if (unixName && *unixName) die_at(141, "Out of luck determining the OS, nor 'pwd' neither 'CD' worked.");
        buf[0] = '\0';
    }
    SB b; sb_init(&b);
    sb_puts(&b, buf);
    sb_puts(&b, ds);
    free(cwd);
    cwd = sb_detach(&b);
}

/* the esd.pm lookup: the directories a Perl @INC would offer, with the original
 * rule that entries containing a dot are ignored */
static int find_esd_pm(void)
{
    static const char *std_dirs[] = {
        "/usr/local/lib/perl5", "/usr/lib/perl5", "/usr/share/perl5",
        "/usr/local/share/perl", "/usr/lib/perl", "/usr/local/lib/perl",
        "/etc/perl", "/usr/share/perl", "/usr/local/share/perl5", NULL
    };
    SB cand; sb_init(&cand);

    /* PERL5LIB entries first, the way Perl prepends them to @INC */
    const char *p5l = getenv("PERL5LIB");
    if (p5l) {
        const char *sep = (unixName && *unixName) ? ":" : ";";
        const char *p = p5l;
        while (*p) {
            const char *e = strstr(p, sep);
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n) {
                char *dir = xmemdup(p, n);
                sb_puts(&cand, dir); sb_putc(&cand, '\n');
                free(dir);
            }
            if (!e) break;
            p = e + 1;
        }
    }
    for (int i = 0; std_dirs[i]; i++) { sb_puts(&cand, std_dirs[i]); sb_putc(&cand, '\n'); }
    const char *home = getenv("HOME");
    if (home) {
        sb_puts(&cand, home); sb_puts(&cand, "/perl\n");
        sb_puts(&cand, home); sb_puts(&cand, "/perl5\n");
    }

    int found = 0;
    char *p = cand.p;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (!strchr(p, '.')) {                       /* next if /\./ */
            SB f; sb_init(&f);
            sb_puts(&f, p);
            sb_puts(&f, "/jamal/esd.pm");
            if (file_exists(f.p)) found = 1;
            sb_free(&f);
        }
        if (!nl) break;
        p = nl + 1;
    }
    sb_free(&cand);
    return found;
}

/* ------------------------------------------------------------------- main -- */

/*
 * The Jamal preprocessor is a C program now (tools/codegen/jamal.c).  The Perl
 * original is kept for the documentation files that use the esd extension
 * module, which a C program cannot load, and it is used as a fallback when the
 * C tool has not been built.  The choice is made here and again after the tools
 * have been built, because the --module path runs the preprocessor too.
 */
static void pick_jamal(void)
{
    int isNT = !(unixName && *unixName);
    const char *tool = isNT ? "bin\\tools\\jamal.exe" : "bin/tools/jamal";
    jamalTool = file_exists(tool) ? tool : "perl jamal.pl";
}

int main(int argc, char **argv)
{
    if (!file_exists("lmt_none.def")) {
        fputs(HD_SETUP_INTRO, stdout);
        fputs("\n", stderr);
        return 2;                    /* Perl exits with $! = ENOENT here */
    }

    parse_options(argc, argv);

    if (optv("--help")) {
        help_main();
        return 0;
    }

    validate_options();
    load_options_file();

    int nt_requested = optv("--nt") && *optv("--nt");
    int unix_requested = optv("--unix") && *optv("--unix");
    int darwin_requested = optv("--darwin") && *optv("--darwin");
    if (nt_requested + unix_requested + darwin_requested > 1)
        die_at(111, "Can only use one of --nt, --unix, --darwin at the same time");

    determine_os();
    set_cwd();
    build = (unixName && *unixName) ? unixName : "nt";

    print_msg("This is %s cwd=%s\n", (unixName && *unixName) ? unixName : "NT", cwd);

    /*
     * Check that esd.pm is installed for Jamal.  Only the documentation chain
     * needs it; the code build itself does not.
     */
    if (!find_esd_pm()) {
        fputs(HD_ESD_MISSING, stdout);
        return 1;
    }

    subst_set("BIN", "");            /* the templates that use them are only */
    subst_set("CACHE", "");          /* interpolated after the install      */
    subst_set("CONFIGDIR", "");      /* variables have been set             */
    subst_set("DOCU", "");
    subst_set("ETC", "");
    subst_set("HEBTEMP", "");
    subst_set("INCLUDE", "");
    subst_set("LIB", "");
    subst_set("LOG", "");
    subst_set("MAXLEVEL", "");
    subst_set("MODULE", "");
    subst_set("SOURCE", "");

    /* the --module subprocess runs the preprocessor too: choose jamal here */
    pick_jamal();

    /* --zip: create the ZIP file creator cmd file */
    if (optv("--zip")) {
        const char *dir = optv("--directory");
        if (!dir) dir = "T:\\ScriptBasic\\";
        SB cmd; sb_init(&cmd);
        sb_puts(&cmd, "perl scriba-zip.pl ");
        sb_puts(&cmd, dir);
        char *out = run_capture(cmd.p);
        sb_free(&cmd);
        fputs(out, stdout);
        free(out);
        fputs(HD_ZIP_DONE, stdout);
        return 0;
    }

    if (optv("--self-check")) {
        self_check();
        return 0;
    }

    if (optv("--module")) {
        configure_module(optv("--module"));
        save_options();
        return 0;
    }

    if (optv("--check")) {
        check_compilation_result();
        save_options();
        return 0;
    }

    if (optv("--install")) {
        if (unixName && *unixName) {
            install_unix_scriba();
            fputs(HD_INSTALL_UNIX, stdout);
            save_options();
            return 0;
        }
        const char *InstallDirectory = optv("--directory");
        if (!InstallDirectory) InstallDirectory = "T:/ScriptBasic/";
        if (file_exists("bin/vc7/exe/scriba.exe")) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "bin\\vc7\\exe\\scriba.exe install.sb %s vc7", InstallDirectory);
            char *out = run_capture(cmd.p);
            sb_free(&cmd);
            fputs(out, stdout);
            free(out);
            save_options();
            return 0;
        }
        if (file_exists("bin/bcc/exe/scriba.exe")) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "bin\\bcc\\exe\\scriba.exe install.sb %s bcc", InstallDirectory);
            char *out = run_capture(cmd.p);
            sb_free(&cmd);
            fputs(out, stdout);
            free(out);
            save_options();
            return 0;
        }
        fputs(HD_INSTALL_NT, stdout);
        save_options();
        return 0;
    }

    /*
     * find out what file make_common.jim includes
     */
    char *configure_jim = NULL;                 /* '' when make_common.jim has none */
    {
        FILE *f = fopen("make_common.jim", "rb");
        if (!f) die_at(282, "Can not open 'make_common.jim'");
        char line[8192];
        while (fgets(line, sizeof line, f)) {
            /* /{#include\s+(.*)}\s*$/ : capture back to the last '}' that has
             * nothing but whitespace after it */
            char *hit = strstr(line, "{#include");
            if (!hit) continue;
            char *p = hit + 9;
            char *w = p;
            while (*w == ' ' || *w == '\t' || *w == '\r' || *w == '\n' || *w == '\f' || *w == '\v') w++;
            if (w == p) continue;                       /* \s+ needs one byte */
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                               line[len - 1] == ' ' || line[len - 1] == '\t')) len--;
            if (len == 0 || line[len - 1] != '}') continue;
            configure_jim = xmemdup(w, (size_t)(line + len - 1 - w));
            break;
        }
        fclose(f);
        if (!configure_jim) configure_jim = xstrdup("");
        if (!*configure_jim) print_msg("No file is included by make_common.jim. Is this setup correct?");
    }

    /*
     * create the file $configure_jim that holds the jamal macro that defines
     * the source root directory and other directories
     */
    if (*configure_jim) {
        print_msg("creating %s\n", configure_jim);
        FILE *f = fopen(configure_jim, "wb");
        if (!f) die_at(299, "Can not create the file %s", configure_jim);
        fprintf(f, "{#define SourceRoot=%s}\n", cwd);
        if (!(unixName && *unixName)) {
            if (opt_true("--no-search-bcc")) {
                const char *v;
                if ((v = optv("--BorlandIncludeDirectory")) && strlen(v) > 0)
                    fprintf(f, "{#define BorlandIncludeDirectory=%s}\n", v);
                if ((v = optv("--BorlandLibDirectory")) && strlen(v) > 0)
                    fprintf(f, "{#define BorlandLibDirectory=%s}\n", v);
                if ((v = optv("--bcc32")) && strlen(v) > 0)
                    fprintf(f, "{#define bcc32=%s}\n", v);
            } else {
                /* search the Borland C compiler along the PATH */
                print_msg("Trying to find out where the Borland compiler is installed (if installed)\n");
                const char *path = getenv("PATH");
                if (path) {
                    const char *p = path;
                    while (*p) {
                        const char *e = strchr(p, ';');
                        size_t n = e ? (size_t)(e - p) : strlen(p);
                        char *dir = xmemdup(p, n);
                        SB cand; sb_init(&cand);
                        sb_puts(&cand, dir); sb_puts(&cand, "\\bcc32.exe");
                        if (file_exists(cand.p)) {
                            print_msg("Borland C compiler is installed in %s\n", dir);
                            /* chop off the 'bin' where Borland puts the executable */
                            size_t dl = strlen(dir);
                            if (dl >= 4 && (dir[dl - 4] == '\\') &&
                                (dir[dl - 3] == 'b' || dir[dl - 3] == 'B') &&
                                (dir[dl - 2] == 'i' || dir[dl - 2] == 'I') &&
                                (dir[dl - 1] == 'n' || dir[dl - 1] == 'N'))
                                dir[dl - 4] = '\0';
                            fprintf(f, "{#define BorlandIncludeDirectory=\"%s\\Include\"}\n", dir);
                            fprintf(f, "{#define BorlandLibDirectory=\"%s\\Lib\"}\n", dir);
                            fprintf(f, "{#define bcc32=\"%s\\bin\\bcc32.exe\"}\n", dir);
                            if (opt_true("--save-bcc-config")) {
                                SB v; sb_init(&v);
                                sb_printf(&v, "%s\\bin\\bcc32.exe", dir);
                                opt_set("--bcc32", v.p);
                                sb_free(&v);
                                sb_init(&v);
                                sb_printf(&v, "%s\\Include", dir);
                                opt_set("--BorlandIncludeDirectory", v.p);
                                sb_free(&v);
                                sb_init(&v);
                                sb_printf(&v, "%s\\Lib", dir);
                                opt_set("--BorlandLibDirectory", v.p);
                                sb_free(&v);
                                if (optv("--save") == NULL) opt_set("--save", "1");
                            }
                            sb_free(&cand);
                            free(dir);
                            break;
                        }
                        sb_free(&cand);
                        free(dir);
                        if (!e) break;
                        p = e + 1;
                    }
                }
            }
        }
        fclose(f);
    }

    /*
     * create the directories that are needed during compilation
     */
    if (!opt_true("--no-mkdir")) {
        print_msg("creating subdirectories for compilation output files\n");
        mkdir_mode("bin", mask_mode);
        mkdir_mode("bin/texi", mask_mode);
        if (unixName && *unixName) {
            const char *dirs[] = { "bin/mod", "bin/mod/obj", "bin/mod/lib", "bin/mod/dll",
                                   "bin/var", "bin/include", "bin/obj", "bin/lib",
                                   "bin/make", "bin/cmd", "bin/exe", NULL };
            for (int i = 0; dirs[i]; i++) mkdir_mode(dirs[i], mask_mode);
        } else {
            static const char *bccdirs[] = { "bin/bcc", "bin/bcc/mod", "bin/bcc/mod/obj",
                "bin/bcc/mod/lib", "bin/bcc/mod/dll", "bin/bcc/var", "bin/bcc/include",
                "bin/bcc/obj", "bin/bcc/lib", "bin/bcc/make", "bin/bcc/cmd", "bin/bcc/exe", NULL };
            static const char *vc7dirs[] = { "bin/vc7", "bin/vc7/mod", "bin/vc7/mod/obj",
                "bin/vc7/mod/lib", "bin/vc7/mod/dll", "bin/vc7/var", "bin/vc7/include",
                "bin/vc7/obj", "bin/vc7/lib", "bin/vc7/make", "bin/vc7/cmd", "bin/vc7/exe", NULL };
            for (int i = 0; bccdirs[i]; i++) mkdir_mode(bccdirs[i], mask_mode);
            for (int i = 0; vc7dirs[i]; i++) mkdir_mode(vc7dirs[i], mask_mode);
        }
    }

    if (!opt_true("--no-tools")) {   /* the code generators are C programs now */
        print_msg("building the C code generator tools in tools/codegen\n");
        logit("make -C tools/codegen");
        if (system("make -C tools/codegen") != 0)
            die_plain("Can not build the C code generator tools in tools/codegen\n");
    }

    pick_jamal();
    if (strcmp(jamalTool, "perl jamal.pl") == 0)
        print_msg("The jamal tool is not built, falling back to perl jamal.pl\n");

    if (!opt_true("--no-makefile")) {   /* default is to create the makefiles */
        print_msg("compiling Makefile using the Jamal preprocessor\n");
        const char *m;
        if (strcmp(unixName, "unix") == 0) m = "make_gcc.jim";
        else if (strcmp(unixName, "darwin") == 0) m = "make_darwin.jim";
        else m = "make_vc7.jim";
        SB cmd; sb_init(&cmd);
        sb_printf(&cmd, "%s -m %s makefile.jam ", jamalTool, m);
        sb_puts(&cmd, (unixName && *unixName) ? "bin/make/Makefile" : "bin\\vc7\\make\\makefile");
        logit(cmd.p);
        run_quiet(cmd.p);
        sb_free(&cmd);
        if (!(unixName && *unixName)) {
            sb_init(&cmd);
            sb_printf(&cmd, "%s -m make_bcc.jim makefile.jam bin\\bcc\\make\\makefile", jamalTool);
            logit(cmd.p);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
    }

    if (!opt_true("--no-syntaxer")) {
        print_msg("running the syntaxer tool to generate the syntax definition C language tables from syntax.def\n");
        logit("bin/tools/syntaxer");
        run_quiet("bin/tools/syntaxer");
    }

    if (!opt_true("--no-generrh")) {
        print_msg("running the generrh tool to generate the error messages from errors.def\n");
        run_quiet("bin/tools/generrh");
    }

    if (!opt_true("--no-lmt_make")) {
        print_msg("running the lmt_make tool for all lmt*.def files\n");
        int n = 0;
        char **files = list_dir(".", &n);
        for (int i = 0; i < n; i++) {
            const char *name = files[i];
            if (strncmp(name, "lmt_", 4) == 0 && strlen(name) > 8 &&
                strcmp(name + strlen(name) - 4, ".def") == 0) {
                SB cmd; sb_init(&cmd);
                sb_printf(&cmd, "bin/tools/lmt_make %s", name);
                run_quiet(cmd.p);
                sb_free(&cmd);
            }
        }
        free_list(files, n);
    }

    if (!opt_true("--no-headerer")) {
        print_msg("scanning all subdirectories to find all C source files\n");
        int nfiles = 0, capp = 64;
        char **files = xmalloc((size_t)capp * sizeof *files);
        int ndirs = 0, capd = 16;
        char **dirs = xmalloc((size_t)capd * sizeof *dirs);

        int n0 = 0;
        char **top = list_dir(".", &n0);
        for (int i = 0; i < n0; i++) {
            if (nfiles == capp) { capp *= 2; files = xrealloc(files, (size_t)capp * sizeof *files); }
            files[nfiles++] = xstrdup(top[i]);
            if (is_dir(top[i]) && top[i][0] != '.') {
                if (ndirs == capd) { capd *= 2; dirs = xrealloc(dirs, (size_t)capd * sizeof *dirs); }
                dirs[ndirs++] = xstrdup(top[i]);
            }
        }
        free_list(top, n0);

        while (ndirs > 0) {
            char *cdir = dirs[--ndirs];                 /* pop: the original pops */
            int nc = 0;
            char **listing = list_dir(cdir, &nc);
            for (int i = 0; i < nc; i++) {
                SB path; sb_init(&path);
                sb_printf(&path, "%s/%s", cdir, listing[i]);
                if (is_dir(path.p) && listing[i][0] != '.') {
                    if (ndirs == capd) { capd *= 2; dirs = xrealloc(dirs, (size_t)capd * sizeof *dirs); }
                    dirs[ndirs++] = xstrdup(path.p);
                }
                if (nfiles == capp) { capp *= 2; files = xrealloc(files, (size_t)capp * sizeof *files); }
                files[nfiles++] = xstrdup(path.p);
                sb_free(&path);
            }
            free(cdir);
            free_list(listing, nc);
        }
        free(dirs);

        print_msg("there are %d files in the source tree\n", nfiles);

        int nc_files = 0;
        for (int i = 0; i < nfiles; i++) {
            size_t l = strlen(files[i]);
            if (l > 2 && files[i][l - 2] == '.' &&
                (files[i][l - 1] == 'c' || files[i][l - 1] == 'C'))
                files[nc_files++] = files[i];
            else
                free(files[i]);
        }
        print_msg("there are %d C source files in the source tree\n", nc_files);

        for (int i = 0; i < nc_files; i++) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "bin/tools/headerer %s", files[i]);
            run_quiet(cmd.p);
            sb_free(&cmd);
            free(files[i]);
        }
        free(files);
    }

    /*
     * configure all modules that are in the system
     */
    int  nModulesSetUp = 0;
    char **modulesSetUp = xmalloc(64 * sizeof *modulesSetUp);
    if (!opt_true("--no-modules")) {
        int n = 0;
        char **extensions = list_dir("extensions", &n);
        for (int i = 0; i < n; i++) {
            const char *e = extensions[i];
            SB d; sb_init(&d);
            sb_printf(&d, "extensions/%s", e);
            int ok = is_dir(d.p) && strcmp(e, "CVS") != 0 &&
                     strcmp(e, ".") != 0 && strcmp(e, "..") != 0;
            sb_free(&d);
            if (!ok) continue;

            print_msg("configuring module %s\n", e);
            SB ntf; sb_init(&ntf);
            if (unixName && *unixName) sb_printf(&ntf, "--%s", unixName);
            else sb_puts(&ntf, "--nt");
            const char *bcc = strcmp(optv("--compile") ? optv("--compile") : "", "bcc") == 0
                            ? "--compile=bcc" : "--compile=vc7";
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "\"%s\" --module=%s %s %s --log-append",
                      argv[0], e, ntf.p, bcc);
            char *out = run_capture(cmd.p);
            fputs(out, stdout);
            free(out);
            sb_free(&cmd);
            sb_free(&ntf);

            int skip = 0;
            SB nt_file; sb_init(&nt_file);
            if (unixName && strcmp(unixName, "unix") == 0) sb_printf(&nt_file, "extensions/%s/notunix.txt", e);
            else if (unixName && strcmp(unixName, "darwin") == 0) sb_printf(&nt_file, "extensions/%s/notdarwin.txt", e);
            else sb_printf(&nt_file, "extensions/%s/notnt.txt", e);
            skip = file_exists(nt_file.p);
            sb_free(&nt_file);

            if (!skip) {
                modulesSetUp[nModulesSetUp++] = xstrdup(e);
            }
        }
        free_list(extensions, n);
    }

    print_msg("making compile command file(s)\n");
    if (unixName && *unixName) {
        SB f; sb_init(&f);
        sb_puts(&f, "#! /bin/sh\n");
        sb_puts(&f, "# this shell file was automatically generated by setup.pl\n");
        sb_puts(&f, "make -f bin/make/Makefile\n");
        sb_puts(&f, "cd extensions\n");
        for (int i = 0; i < nModulesSetUp; i++) {
            sb_printf(&f, "cd %s\n", modulesSetUp[i]);
            sb_puts(&f, "make -f makefile\n");
            sb_puts(&f, "cd ..\n");
        }
        sb_puts(&f, "cd ..\n");
        sb_puts(&f, "if [ -e bin/mod/lib/cgi.a ] && [ -e bin/mod/lib/mt.a ]\n");
        sb_puts(&f, "then\n");
        sb_puts(&f, "  make -f bin/make/Makefile eszter\n");
        sb_puts(&f, "fi\n");
        if (!write_file_mode("compile", f.p, 0)) die_at(532, "Can not write file 'compile'");
        chmod_path("compile", 0700);
        sb_free(&f);
        if (!opt_true("--no-compile")) {
            print_msg("starting compilation\n");
            run_quiet("./compile");
        }
    } else {
        SB f; sb_init(&f);
        print_msg("Creating compile_vc7.cmd\n");
        sb_puts(&f, "REM this command file was automatically generated by setup.pl\n");
        sb_puts(&f, "nmake -f bin\\vc7\\make\\makefile\n");
        sb_puts(&f, "cd extensions\n");
        for (int i = 0; i < nModulesSetUp; i++) {
            sb_printf(&f, "cd %s\n", modulesSetUp[i]);
            sb_puts(&f, "nmake -f makefile.vc7\n");
            sb_puts(&f, "cd ..\n");
        }
        sb_puts(&f, "cd ..\n");
        if (!write_file_mode("compile_vc7.cmd", f.p, 0)) die_at(555, "Can not write compile_vc7.cmd");
        sb_free(&f);

        print_msg("Creating compile_bcc.cmd\n");
        sb_init(&f);
        sb_puts(&f, "REM this command file was automatically generated by setup.pl\n");
        sb_puts(&f, "make -f bin\\bcc\\make\\makefile\n");
        sb_puts(&f, "cd extensions\n");
        for (int i = 0; i < nModulesSetUp; i++) {
            sb_printf(&f, "cd %s\n", modulesSetUp[i]);
            sb_puts(&f, "make -f makefile.bcc\n");
            sb_puts(&f, "cd ..\n");
        }
        sb_puts(&f, "cd ..\n");
        if (!write_file_mode("compile_bcc.cmd", f.p, 0)) die_at(568, "Can not write compile_bcc.cmd");
        sb_free(&f);

        if (!opt_true("--no-compile")) {
            print_msg("starting compilation\n");
            if (strcmp(optv("--compile") ? optv("--compile") : "", "bcc") == 0)
                run_quiet("cmd /c compile_bcc.cmd");
            else
                run_quiet("cmd /c compile_vc7.cmd");
        }
    }

    save_options();

    if (opt_true("--no-compile")) {
        if (unixName && *unixName) print_msg("Setup was finished. Now you can run './compile'\n");
        else print_msg("Setup was finished. Now you can run 'compile_vc7' or 'compile_bcc'\n");

        fputs(HD_CORE_NOTE, stdout);

        int n = 0;
        char **extensions = list_dir("extensions", &n);
        if (!extensions) die_at(615, "Can not open the directory 'extensions'");
        for (int i = 0; i < n; i++) {
            const char *e = extensions[i];
            if (!is_dir(extensions[i])) continue;
            SB d; sb_init(&d);
            sb_printf(&d, "extensions/%s", e);
            int ok = is_dir(d.p) && strcmp(e, ".") != 0 && strcmp(e, "..") != 0 && strcmp(e, "CVS") != 0;
            sb_free(&d);
            if (!ok) continue;

            SB lib; sb_init(&lib);
            sb_printf(&lib, "extensions/%s/libraries.jim", e);
            if (file_exists(lib.p)) {
                print_msg("Module %s", e);
                for (size_t k = strlen(e); k < 8; k++) print_msg(" ");
                print_msg(" needs ");
                FILE *f = fopen(lib.p, "rb");
                if (!f) die_at(623, "Can not read 'extensions/%s/libraries.jim'", e);
                char line[1024];
                char *q = NULL;
                if (fgets(line, sizeof line, f)) {
                    size_t l = strlen(line);
                    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
                    q = xstrdup(line);
                    /* $q =~ s/^.*\=(.*)\}/$1/ -- the last '=' and the last '}' */
                    char *eq = strrchr(q, '=');
                    char *br = strrchr(q, '}');
                    if (eq && br && br > eq) {
                        char *v = xmemdup(eq + 1, (size_t)(br - eq - 1));
                        free(q);
                        q = v;
                    }
                    if (!*q && fgets(line, sizeof line, f)) {
                        size_t l2 = strlen(line);
                        while (l2 > 0 && (line[l2 - 1] == '\n' || line[l2 - 1] == '\r')) line[--l2] = '\0';
                        free(q);
                        q = xstrdup(line);
                        char *eq2 = strrchr(q, '=');
                        char *br2 = strrchr(q, '}');
                        if (eq2 && br2 && br2 > eq2) {
                            char *v = xmemdup(eq2 + 1, (size_t)(br2 - eq2 - 1));
                            free(q);
                            q = v;
                        }
                    }
                }
                fclose(f);
                print_msg("%s\n", q ? q : "");
                free(q);
            }
            sb_free(&lib);
        }
        free_list(extensions, n);
    } else {
        check_compilation_result();
    }

    for (int i = 0; i < nModulesSetUp; i++) free(modulesSetUp[i]);
    free(modulesSetUp);
    if (LogFile) fclose(LogFile);
    return 0;
}

/* ==================================================================== subs == */

static void save_options(void)
{
    if (!opt_true("--save")) return;
    if (strcmp(optv("--save"), "1") == 0) opt_set("--save", "configure.save");
    print_msg("Saving configuration info to %s\n", optv("--save"));
    FILE *f = fopen(optv("--save"), "wb");
    if (!f) die_at(731, "Can not save configuration to file %s", optv("--save"));
    /* sorted keys, as the original prints them */
    int *idx = xmalloc((size_t)(nOpt ? nOpt : 1) * sizeof *idx);
    for (int i = 0; i < nOpt; i++) idx[i] = i;
    for (int i = 1; i < nOpt; i++) {                 /* insertion sort on names */
        int k = idx[i], j = i - 1;
        while (j >= 0 && strcmp(Opt[idx[j]].name, Opt[k].name) > 0) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = k;
    }
    for (int i = 0; i < nOpt; i++) {
        const char *name = Opt[idx[i]].name;
        if (strcmp(name, "--save") == 0) continue;
        if (strcmp(name, "--load") == 0) continue;
        fprintf(f, "%s=%s\n", name, Opt[idx[i]].value);
    }
    free(idx);
    fclose(f);
}

static void check_compilation_result(void)
{
    int dll = 0, lib = 0, bas = 0;

    if (unixName && *unixName) {
        print_msg("scriba executable %s\n", file_exists("bin/exe/scriba") ? "OK  " : "FAIL");
        print_msg("sbhttpd executable %s\n", file_exists("bin/exe/sbhttpd") ? "OK  " : "FAIL");
        print_msg("libscriba library %s\n", file_exists("bin/lib/libscriba.a") ? "OK  " : "FAIL");

        int n = 0;
        char **modules = list_dir("extensions", &n);
        if (!modules) die_at(654, "Can not list directory 'extensions'");
        FILE *out = fopen("modinst.sh", "wb");
        if (!out) die_at(654, "Can not write modinst.sh");
        for (int i = 0; i < n; i++) {
            const char *m = modules[i];
            SB d; sb_init(&d);
            sb_printf(&d, "extensions/%s", m);
            int isdir = is_dir(d.p);
            sb_free(&d);
            if (!isdir) continue;
            if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0 || strcmp(m, "CVS") == 0) continue;

            SB nt; sb_init(&nt);
            sb_printf(&nt, "extensions/%s/not%s.txt", m,
                      strcmp(unixName, "darwin") == 0 ? "darwin" : "unix");
            int skip = file_exists(nt.p);
            sb_free(&nt);
            if (skip) continue;

            print_msg("MODULE %s: ", m);
            for (size_t k = strlen(m); k < 8; k++) print_msg(" ");
            SB f; sb_init(&f);
            if (strcmp(unixName, "darwin") == 0) sb_printf(&f, "bin/mod/dll/%s.dylib", m);
            else sb_printf(&f, "bin/mod/dll/%s.so", m);
            dll = file_exists(f.p);
            print_msg("dll %s", dll ? "OK   " : "FAIL ");
            sb_free(&f);
            sb_init(&f);
            sb_printf(&f, "bin/mod/lib/%s.a", m);
            lib = file_exists(f.p);
            print_msg("lib %s", lib ? "OK   " : "FAIL ");
            sb_free(&f);
            sb_init(&f);
            sb_printf(&f, "extensions/%s/%s.bas", m, m);
            bas = file_exists(f.p);
            print_msg("bas %s", bas ? "OK  " : "FAIL");
            sb_free(&f);
            print_msg("\n");
            if (dll && lib && bas) fprintf(out, "cd %s\nscriba -n install.sb\ncd ..\n", m);
        }
        fprintf(out, "cd ..\n");
        fclose(out);
        chmod_path("modinst.sh", 0700);
        free_list(modules, n);
    } else {
        const char *compile = optv("--compile");
        const char *compiler = (compile && strcmp(compile, "bcc") == 0) ? "bcc" : "vc7";
        SB f; sb_init(&f);
        sb_printf(&f, "bin/%s/exe/scriba.exe", compiler);
        print_msg("scriba.exe %s\n", file_exists(f.p) ? "OK  " : "FAIL");
        sb_free(&f);
        sb_init(&f);
        sb_printf(&f, "bin/%s/exe/sbhttpd.exe", compiler);
        print_msg("sbhttpd.exe %s\n", file_exists(f.p) ? "OK  " : "FAIL");
        sb_free(&f);
        sb_init(&f);
        sb_printf(&f, "bin/%s/lib/libscriba.lib", compiler);
        print_msg("libscriba.lib %s\n", file_exists(f.p) ? "OK  " : "FAIL");
        sb_free(&f);

        int n = 0;
        char **modules = list_dir("extensions", &n);
        if (!modules) die_at(694, "Can not list directory 'extensions'");
        FILE *out = fopen("modinst.cmd", "wb");
        if (!out) die_at(694, "Can not write modinst.cmd");
        fprintf(out, "cd extensions\n");
        int errorFlag = 0;
        for (int i = 0; i < n; i++) {
            const char *m = modules[i];
            SB d; sb_init(&d);
            sb_printf(&d, "extensions/%s", m);
            int isdir = is_dir(d.p);
            sb_free(&d);
            if (!isdir) continue;
            if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0 || strcmp(m, "CVS") == 0) continue;
            sb_init(&d);
            sb_printf(&d, "extensions/%s/notnt.txt", m);
            int skip = file_exists(d.p);
            sb_free(&d);
            if (skip) continue;

            print_msg("MODULE %s: ", m);
            for (size_t k = strlen(m); k < 8; k++) print_msg(" ");
            SB f2; sb_init(&f2);
            sb_printf(&f2, "bin/%s/mod/dll/%s.dll", compiler, m);
            dll = file_exists(f2.p);

            sb_free(&f2);
            print_msg("dll %s", dll ? "OK   " : "FAIL ");
            sb_init(&f2);
            sb_printf(&f2, "bin/%s/mod/lib/%s.lib", compiler, m);
            lib = file_exists(f2.p);
            print_msg("lib %s", lib ? "OK   " : "FAIL ");
            sb_free(&f2);
            sb_init(&f2);
            sb_printf(&f2, "extensions/%s/%s.bas", m, m);
            bas = file_exists(f2.p);
            print_msg("bas %s", bas ? "OK  " : "FAIL");
            sb_free(&f2);
            print_msg("\n");
            if (dll && lib && bas) fprintf(out, "cd %s\nscriba -n install.sb\ncd ..\n", m);
            if (!(dll && lib && bas)) errorFlag = 1;
        }
        fprintf(out, "cd ..\n");
        fclose(out);
        (void)errorFlag;
        free_list(modules, n);
        print_msg("\n");
        print_msg("Some of the modules were not compiled. They may depend on extra\n");
        print_msg("libraries that have to be compiled and installed before the module.\n");
        print_msg("Check the libraries that the modules need!\n");
    }
}

/* ------------------------------------------------------------------ module -- */

static void create_skeleton_file(const char *module, const char *file)
{
    const char *content = NULL;
    for (int i = 0; i < SETUP_SKELETON_COUNT; i++)
        if (strcmp(SETUP_SKELETONS[i].name, file) == 0) content = SETUP_SKELETONS[i].text;
    if (!content) return;

    /* uc() of the module name */
    SB upper; sb_init(&upper);
    for (const char *p = module; *p; p++) sb_putc(&upper, (char)toupper((unsigned char)*p));

    /* the original substitutes $module, $MODULE and $date; $date is never set,
     * so it disappears */
    SB text; sb_init(&text);
    for (const char *p = content; *p; ) {
        if (strncmp(p, "$module", 7) == 0) { sb_puts(&text, module); p += 7; continue; }
        if (strncmp(p, "$MODULE", 7) == 0) { sb_puts(&text, upper.p); p += 7; continue; }
        if (strncmp(p, "$date", 5) == 0) { p += 5; continue; }
        sb_putc(&text, *p++);
    }
    sb_free(&upper);

    SB openfile; sb_init(&openfile);
    sb_printf(&openfile, "extensions/%s/%s", module, file);
    if (!file_exists(openfile.p)) {
        if (!write_file_mode(openfile.p, text.p, 0)) die_at(950, "Can not create %s", openfile.p);
    }
    sb_free(&openfile);
    sb_free(&text);
}

static void create_module_skeleton(const char *module)
{
    for (int i = 0; i < SETUP_SKELETON_COUNT; i++)
        create_skeleton_file(module, SETUP_SKELETONS[i].name);
}

/* /^\s*\#\s*include\s+"([\w\/]+)\.h"\s*$/ -- a local header dependency */
static int include_dep(const char *line, char **name)
{
    const char *p = line;
    while (is_space(*p)) p++;
    if (*p != '#') return 0;
    p++;
    while (is_space(*p)) p++;
    if (strncmp(p, "include", 7) != 0) return 0;
    p += 7;
    const char *w = p;
    while (is_space(*w)) w++;
    if (w == p) return 0;
    p = w;
    if (*p != '"') return 0;
    p++;
    const char *s = p;
    while (isalnum((unsigned char)*p) || *p == '_' || *p == '/') p++;
    if (p == s) return 0;
    const char *e = p;
    if (p[0] != '.' || p[1] != 'h' || p[2] != '"') return 0;
    p += 3;
    while (is_space(*p)) p++;
    if (*p != '\0') return 0;
    *name = xmemdup(s, (size_t)(e - s));
    return 1;
}

static void create_makefile_jam(const char *module)
{
    SB jampath; sb_init(&jampath);
    sb_printf(&jampath, "extensions/%s/makefile.jam", module);
    FILE *f = fopen(jampath.p, "wb");
    if (!f) die_at(978, "Can not create %s", jampath.p);
    sb_free(&jampath);

    /* the header of the makefile, with $module interpolated */
    subst_set("module", module);
    char *head = interp(HD_MAKEFILE_JAM);
    fputs(head, f);
    free(head);

    /* include libraries.jim when the interface.c mentions libraries */
    int libsflag = 1;
    SB iface; sb_init(&iface);
    sb_printf(&iface, "extensions/%s/interface.c", module);
    FILE *cf = fopen(iface.p, "rb");
    if (cf) {
        print_msg("Searching for libraries\n");
        char line[8192];
        while (fgets(line, sizeof line, cf)) {
            if (strncmp(line, "UXLIBS", 6) == 0 || strncmp(line, "NTLIBS", 6) == 0) {
                fputs("{#include libraries.jim}\n", f);
                libsflag = 0;
                break;
            }
        }
        fclose(cf);
    }
    sb_free(&iface);
    if (libsflag)
        fputs("{#define LibNeededLibraryFiles=}{#define LdNeededLibraryFiles=}\n", f);

    /* the .c files of the module, in readdir order, extension chopped */
    SB moddir; sb_init(&moddir);
    sb_printf(&moddir, "extensions/%s", module);
    int nall = 0;
    char **all = list_dir(moddir.p, &nall);
    if (!all) die_at(1009, "Can not read the directory %s", moddir.p);
    int nc = 0;
    char **csrc = xmalloc((size_t)(nall ? nall : 1) * sizeof *csrc);
    for (int i = 0; i < nall; i++) {
        size_t l = strlen(all[i]);
        if (l > 2 && all[i][l - 2] == '.' && all[i][l - 1] == 'c')
            csrc[nc++] = xmemdup(all[i], l - 2);
    }
    /* the original prints the name before it chops the extension off */
    for (int i = 0; i < nc; i++) print_msg("file: %s.c\n", csrc[i]);
    free_list(all, nall);

    /* the target line of the module library */
    fprintf(f, "{CreLib %s} : ", module);
    for (int i = 0; i < nc; i++) {
        if (strcmp(csrc[i], "interface") == 0) fprintf(f, "{CreSObj %s} ", csrc[i]);
        else fprintf(f, "{CreObj %s} ", csrc[i]);
    }
    fprintf(f, "\n");
    fprintf(f, "\t{lib} {LibOptOutput {CreLib %s}} ", module);
    for (int i = 0; i < nc; i++) {
        if (strcmp(csrc[i], "interface") == 0) fprintf(f, "{LibOptInput {CreSObj %s}} ", csrc[i]);
        else fprintf(f, "{LibOptInput {CreObj %s}} ", csrc[i]);
    }
    fprintf(f, " {LibNeededLibraryFiles}\n\n");

    /* the target line of the shared library */
    fprintf(f, "{CreDll %s} : ", module);
    for (int i = 0; i < nc; i++) fprintf(f, "{CreObj %s} ", csrc[i]);
    fprintf(f, "\n");
    fprintf(f, "\t{ld} {LdOptOutput {CreDll %s}} ", module);
    for (int i = 0; i < nc; i++) fprintf(f, "{LdOptInput {CreObj %s}} ", csrc[i]);
    fprintf(f, "{LdNeededLibraryFiles}\n\n");

    /* the .c -> .obj compilation lines */
    for (int i = 0; i < nc; i++) {
        /* the headers this source includes; the original iterates a hash, this
         * port keeps them in the order they are found, which is reproducible */
        int ndep = 0, capdep = 8;
        char **dep = xmalloc((size_t)capdep * sizeof *dep);
        SB src; sb_init(&src);
        sb_printf(&src, "%s/%s.c", moddir.p, csrc[i]);
        FILE *sf = fopen(src.p, "rb");
        if (sf) {
            print_msg("determining dependency from %s\n", src.p);
            char line[8192];
            while (fgets(line, sizeof line, sf)) {
                size_t l = strlen(line);
                while (l > 0 && line[l - 1] == '\n') line[--l] = '\0';
                char *name = NULL;
                if (include_dep(line, &name)) {
                    int seen = 0;
                    for (int k = 0; k < ndep; k++) if (strcmp(dep[k], name) == 0) seen = 1;
                    if (!seen) {
                        if (ndep == capdep) { capdep *= 2; dep = xrealloc(dep, (size_t)capdep * sizeof *dep); }
                        dep[ndep++] = name;
                    } else free(name);
                }
            }
            fclose(sf);
        }
        sb_free(&src);

        fprintf(f, "{CreObj %s} : %s.c ", csrc[i], csrc[i]);
        for (int k = 0; k < ndep; k++) fprintf(f, "%s.h ", dep[k]);
        fprintf(f, "\n");
        fprintf(f, "\t{cc} {CcOptCompile} {CcOptOutput {CreObj %s}} {CcOptInput %s.c}", csrc[i], csrc[i]);
        fprintf(f, "\n\n");

        if (strcmp(csrc[i], "interface") == 0) {
            fprintf(f, "{CreSObj interface} : interface.c ");
            for (int k = 0; k < ndep; k++) fprintf(f, "%s.h ", dep[k]);
            fprintf(f, "\n");
            fprintf(f, "\t{cc} {CcOptDefine STATIC_LINK=1} {CcOptCompile} "
                       "{CcOptOutput {CreSObj interface}} {CcOptInput interface.c}");
            fprintf(f, "\n\n");
        }
        free_list(dep, ndep);
    }
    fprintf(f, "{CompileTexiDoc}\n\n");
    fclose(f);
    free_list(csrc, nc);
    sb_free(&moddir);
}

static void configure_module(const char *module)
{
    mkdir_mode("extensions", mask_mode);
    SB d; sb_init(&d);
    sb_printf(&d, "extensions/%s", module);
    if (!file_exists(d.p)) mkdir_mode(d.p, mask_mode);
    sb_free(&d);

    create_module_skeleton(module);

    /* create libraries.jim when the interface asks for libraries */
    SB iface; sb_init(&iface);
    sb_printf(&iface, "extensions/%s/interface.c", module);
    FILE *f = fopen(iface.p, "rb");
    if (!f) die_at(761, "Can not open %s", iface.p);
    sb_free(&iface);

    char *libopt = NULL;
    const char *tag = NULL;
    if (unixName && strcmp(unixName, "unix") == 0) tag = "UXLIBS:";
    else if (unixName && strcmp(unixName, "darwin") == 0) tag = "DWLIBS:";
    else tag = "NTLIBS:";
    {
        char line[8192];
        while (fgets(line, sizeof line, f)) {
            size_t l = strlen(line);
            while (l > 0 && line[l - 1] == '\n') line[--l] = '\0';   /* chomp */
            size_t tl = strlen(tag);
            if (strncmp(line, tag, tl) == 0) {
                const char *p = line + tl;
                while (is_space(*p)) p++;
                libopt = xstrdup(*p ? p : " ");
                break;
            }
        }
    }
    fclose(f);

    if (libopt) {
        SB lib; sb_init(&lib);
        sb_printf(&lib, "extensions/%s/libraries.jim", module);
        FILE *out = fopen(lib.p, "wb");
        if (!out) die_at(787, "there is a need, but I can not create %s", lib.p);
        fprintf(out, "{#define LdNeededLibraryFiles=%s}\n", libopt);
        fprintf(out, "{#define LibNeededLibraryFiles=%s}\n",
                (unixName && *unixName) ? "" : libopt);
        fclose(out);
        sb_free(&lib);
        free(libopt);
    } else {
        SB notf; sb_init(&notf);
        sb_printf(&notf, "extensions/%s/not%s.txt", module, build);
        FILE *out = fopen(notf.p, "wb");
        if (!out) die_at(794, "Can not write the module file 'not%s.txt'", build);
        fprintf(out, "This file helps setup.pl to know that this module cannot be "
                     "compiled under %s.\n", build);
        fclose(out);
        sb_free(&notf);
        print_msg("The module %s is not for %s.\n", module, build);
    }

    /* run the headerer for the C files of the module */
    SB moddir; sb_init(&moddir);
    sb_printf(&moddir, "extensions/%s", module);
    int nall = 0;
    char **all = list_dir(moddir.p, &nall);
    if (!all) die_at(802, "Can not open module directory %s", moddir.p);
    print_msg("executing headerer for the C files\n");
    for (int i = 0; i < nall; i++) {
        size_t l = strlen(all[i]);
        if (l > 2 && all[i][l - 2] == '.' && all[i][l - 1] == 'c') {
            print_msg(" extracting header from %s\n", all[i]);
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "bin/tools/headerer %s/%s", moddir.p, all[i]);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
    }
    free_list(all, nall);

    print_msg("creating the module object directory\n");
    mkdir_mode("bin", mask_mode);
    if (unixName && *unixName) {
        mkdir_mode("bin/mod", mask_mode);
        mkdir_mode("bin/mod/obj", mask_mode);
        SB o; sb_init(&o);
        sb_printf(&o, "bin/mod/obj/%s", module);
        mkdir_mode(o.p, mask_mode);
        sb_free(&o);
    } else {
        const char *comps[] = { "vc7", "bcc", NULL };
        for (int i = 0; comps[i]; i++) {
            SB p; sb_init(&p);
            sb_printf(&p, "bin/%s", comps[i]); mkdir_mode(p.p, mask_mode); sb_free(&p);
            sb_init(&p);
            sb_printf(&p, "bin/%s/mod", comps[i]); mkdir_mode(p.p, mask_mode); sb_free(&p);
            sb_init(&p);
            sb_printf(&p, "bin/%s/mod/obj", comps[i]); mkdir_mode(p.p, mask_mode); sb_free(&p);
            sb_init(&p);
            sb_printf(&p, "bin/%s/mod/obj/%s", comps[i], module); mkdir_mode(p.p, mask_mode); sb_free(&p);
        }
    }

    SB mkj; sb_init(&mkj);
    sb_printf(&mkj, "extensions/%s/makefile.jam", module);
    if (!file_exists(mkj.p)) {
        print_msg("Creating typical makefile.jam for the module\n");
        create_makefile_jam(module);
    }
    sb_free(&mkj);

    /* run the Jamal preprocessor for the module makefile */
    SB notus; sb_init(&notus);
    if (unixName && strcmp(unixName, "unix") == 0) {
        sb_printf(&notus, "extensions/%s/notunix.txt", module);
        print_msg("Processing jamal files creating makefile\n");
        if (!file_exists(notus.p)) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "%s -m make_gcc.jim %s/makefile.jam %s/makefile",
                      jamalTool, moddir.p, moddir.p);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
    } else if (unixName && strcmp(unixName, "darwin") == 0) {
        sb_printf(&notus, "extensions/%s/notdarwin.txt", module);
        print_msg("Processing jamal files creating makefile\n");
        if (!file_exists(notus.p)) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "%s -m make_darwin.jim %s/makefile.jam %s/makefile",
                      jamalTool, moddir.p, moddir.p);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
    } else {
        SB nt; sb_init(&nt);
        sb_printf(&nt, "extensions/%s/notnt.txt", module);
        print_msg("Processing jamal files creating makefile.vc7\n");
        if (!file_exists(nt.p)) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "%s -m make_vc7.jim %s/makefile.jam %s/makefile.vc7",
                      jamalTool, moddir.p, moddir.p);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
        print_msg("Processing jamal files creating makefile.bcc\n");
        if (!file_exists(nt.p)) {
            SB cmd; sb_init(&cmd);
            sb_printf(&cmd, "%s -m make_bcc.jim %s/makefile.jam %s/makefile.bcc",
                      jamalTool, moddir.p, moddir.p);
            run_quiet(cmd.p);
            sb_free(&cmd);
        }
        sb_free(&nt);
    }
    sb_free(&notus);

    /* compile the module */
    if (!opt_true("--no-compile")) {
        if (unixName && *unixName) {
            SB sh; sb_init(&sh);
            sb_printf(&sh, "cd extensions/%s\nmake\ncd ../..\n", module);
            if (!write_file_mode("modmake.sh", sh.p, 0)) die_at(862, "Can not write 'modmake.sh'");
            sb_free(&sh);
            SB notu; sb_init(&notu);
            if (strcmp(unixName, "darwin") == 0) sb_printf(&notu, "extensions/%s/notdarwin.txt", module);
            else sb_printf(&notu, "extensions/%s/notunix.txt", module);
            if (!file_exists(notu.p)) {
                run_quiet("/bin/sh modmake.sh");
                SB lib; sb_init(&lib);
                sb_printf(&lib, "%sbin/mod/lib/%s.a", cwd, module);
                SB dll; sb_init(&dll);
                if (strcmp(unixName, "darwin") == 0) sb_printf(&dll, "%sbin/mod/dll/%s.dylib", cwd, module);
                else sb_printf(&dll, "%sbin/mod/dll/%s.so", cwd, module);
                int haveLib = file_exists(lib.p), haveDll = file_exists(dll.p);
                if (haveLib && haveDll) print_msg("Module was compiled fine\n");
                else if (!haveLib && !haveDll) print_msg("ERROR: The module did not compile\n");
                else {
                    if (!haveLib) print_msg("ERROR: Static library was not created for some reason.\n");
                    if (!haveDll) print_msg("ERROR: SO was not created for some reason.\n");
                }
                sb_free(&lib);
                sb_free(&dll);
            }
            sb_free(&notu);
        } else {
            SB cmd; sb_init(&cmd);
            if (strcmp(optv("--compile") ? optv("--compile") : "", "bcc") == 0)
                sb_printf(&cmd, "cd extensions\\%s\nmake -f makefile.bcc\ncd ..\\..\n", module);
            else
                sb_printf(&cmd, "cd extensions\\%s\nnmake -f makefile.vc7\ncd ..\\..\n", module);
            if (!write_file_mode("modmake.cmd", cmd.p, 0)) die_at(888, "Can not write modmake.cmd");
            sb_free(&cmd);

            SB notnt; sb_init(&notnt);
            sb_printf(&notnt, "extensions/%s/notnt.txt", module);
            if (file_exists(notnt.p)) {
                print_msg("The module is for UNIX operating system only, can not be compiled under NT\n");
            } else {
                print_msg("Compiling the module executing modmake.cmd\n");
                run_quiet("cmd /c modmake.cmd");
                const char *comp = strcmp(optv("--compile") ? optv("--compile") : "", "bcc") == 0 ? "bcc" : "vc7";
                SB lib; sb_init(&lib);
                sb_printf(&lib, "%sbin\\%s\\mod\\lib\\%s.lib", cwd, comp, module);
                SB dll; sb_init(&dll);
                sb_printf(&dll, "%sbin\\%s\\mod\\dll\\%s.dll", cwd, comp, module);
                int haveLib = file_exists(lib.p), haveDll = file_exists(dll.p);
                if (haveLib && haveDll) print_msg("Module was compiled fine\n");
                else if (!haveLib && !haveDll) print_msg("ERROR: The module did not compile\n");
                else {
                    if (!haveLib) print_msg("ERROR: Static library was not created for some reason.\n");
                    if (!haveDll) print_msg("ERROR: DLL was not created for some reason.\n");
                }
                sb_free(&lib);
                sb_free(&dll);
            }
            sb_free(&notnt);
        }
    }
    sb_free(&moddir);
}

/* -------------------------------------------------------------- install ---- */

static char *ius_CreateDirectory(const char *optname, const char *permission)
{
    const char *dir = optv(optname);
    subst_set("dir", dir ? dir : "");
    subst_set("permission", permission);
    return interp(HD_IUS_DIR);
}

static char *ius_CopyFile(const char *file, const char *to, const char *owner, const char *permission)
{
    subst_set("file", file);
    subst_set("to", to);
    subst_set("owner", owner);
    subst_set("permission", permission);
    return interp(HD_IUS_COPYFILE);
}

static char *ius_CopyFiles(const char *file, const char *destination, const char *tofils,
                           const char *owner, const char *permission)
{
    subst_set("file", file);
    subst_set("destination", destination);
    subst_set("tofils", tofils);
    subst_set("owner", owner);
    subst_set("permission", permission);
    return interp(HD_IUS_COPYFILES);
}

/* assign the installation variables and mirror them into the interpolation
 * table used by the shell templates */
static void install_set_vars(void)
{
    CONFIGDIR = xstrdup(optv("--install-configdir"));
    INCLUDE   = xstrdup(optv("--install-include"));
    SOURCE    = xstrdup(optv("--install-source"));
    MODULE    = xstrdup(optv("--install-module"));
    DOCU      = xstrdup(optv("--install-docu"));
    LIB       = xstrdup(optv("--install-lib"));
    CACHE     = xstrdup(optv("--install-cache"));
    HEBTEMP   = xstrdup(optv("--install-hebtemp"));
    BIN       = xstrdup(optv("--install-bin"));
    LOG       = xstrdup(optv("--install-log"));
    ETC       = xstrdup(optv("--install-etc"));

    subst_set("CONFIGDIR", CONFIGDIR);
    subst_set("INCLUDE", INCLUDE);
    subst_set("SOURCE", SOURCE);
    subst_set("MODULE", MODULE);
    subst_set("DOCU", DOCU);
    subst_set("LIB", LIB);
    subst_set("CACHE", CACHE);
    subst_set("HEBTEMP", HEBTEMP);
    subst_set("BIN", BIN);
    subst_set("LOG", LOG);
    subst_set("ETC", ETC);
}

/* Perl reads the first line of depth.txt without chomping it */
static char *read_first_line_raw(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    SB b; sb_init(&b);
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') sb_putc(&b, (char)c);
    if (c == '\n') sb_putc(&b, '\n');
    fclose(f);
    return sb_detach(&b);
}

static void install_unix_scriba(void)
{
    if (!file_exists("./bin/exe/scriba"))
        die_at(1195, "./bin/exe/scriba does not exists. Was ScriptBasic compiled?");

    chmod_path("install.sh", 0700);
    unlink_path("install.sh");

    for (int i = 0; i < SETUP_HELP_COUNT; i++) {
        const char *key = SETUP_HELP[i].name;
        if (strncmp(key, "--install-", 10) != 0) continue;
        if (strcmp(key, "--install-interactive") == 0) continue;
        if (optv(key)) continue;
        if (opt_true("--no-install-interactive")) {
            const char *def = install_default_for(key);
            opt_set(key, def ? def : "");
        } else {
            fputs(SETUP_HELP[i].text, stdout);
            fputs("please specify the value:", stdout);
            fflush(stdout);
            char answer[4096];
            if (!fgets(answer, sizeof answer, stdin)) answer[0] = '\0';
            size_t l = strlen(answer);
            while (l > 0 && (answer[l - 1] == '\n' || answer[l - 1] == '\r')) answer[--l] = '\0';
            const char *s = answer;
            while (*s == ' ' || *s == '\t') s++;
            SB t; sb_init(&t);
            sb_puts(&t, s);
            while (t.len > 0 && (t.p[t.len - 1] == ' ' || t.p[t.len - 1] == '\t')) t.p[--t.len] = '\0';
            const char *def = install_default_for(key);
            opt_set(key, *t.p ? t.p : (def ? def : ""));
            sb_free(&t);
        }
    }

    FILE *F = fopen("install.sh", "wb");
    if (!F) die_at(1217, "Can not write 'install.sh'");
    fputs("#! /bin/sh\n", F);
    fputs(HD_INSTALL_SH_HEAD, F);
    fputs("ERRCOUNT=0\n", F);

    char *s;
    s = ius_CreateDirectory("--install-log", "666");       fputs(s, F); free(s);
    s = ius_CreateDirectory("--install-configdir", "555"); fputs(s, F); free(s);
    s = ius_CreateDirectory("--install-include", "555");   fputs(s, F); free(s);
    s = ius_CreateDirectory("--install-source", "555");    fputs(s, F); free(s);
    s = ius_CreateDirectory("--install-module", "555");    fputs(s, F); free(s);
    s = ius_CreateDirectory("--install-lib", "655");       fputs(s, F); free(s);

    install_set_vars();
    subst_set("MAXLEVEL", "");

    fputs_interp(HD_INSTALL_SH_CACHE, F);

    s = ius_CopyFile("./bin/exe/sbhttpd", strf("%s/sbhttpd", BIN), "root", "555");
    fputs(s, F); free(s);
    fprintf(F, "echo \"copying the file sbhttpd shell script to %s\"\n", ETC);
    s = ius_CopyFile("./etc-init.d-sbhttpd", strf("%s/sbhttpd", ETC), "root", "555");
    fputs(s, F); free(s);
    fputs_interp(HD_INSTALL_SH_CONF, F);

    fputs("echo \"copying the standard header files\"\n", F);
    s = ius_CopyFiles("include/*.bas", INCLUDE, strf("%s/*.bas", INCLUDE), "root", "444");
    fputs(s, F); free(s);
    fputs("echo \"installing all modules that were successfully compiled\"\n", F);

    /* start the BASIC installation scripts of every compiled module */
    int n = 0;
    char **modules = list_dir("extensions", &n);
    if (!modules) die_at(1351, "Can not list directory 'extensions'");
    for (int i = 0; i < n; i++) {
        const char *m = modules[i];
        SB d; sb_init(&d);
        sb_printf(&d, "extensions/%s", m);
        int isdir = is_dir(d.p);
        sb_free(&d);
        if (!isdir) continue;
        if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0 || strcmp(m, "CVS") == 0) continue;
        SB p; sb_init(&p);
        sb_printf(&p, "extensions/%s/notunix.txt", m);
        int skip = file_exists(p.p);
        sb_free(&p);
        if (skip) continue;

        SB so, lib, bas, inst;
        sb_init(&so);   sb_printf(&so, "bin/mod/dll/%s.so", m);
        sb_init(&lib);  sb_printf(&lib, "bin/mod/lib/%s.a", m);
        sb_init(&bas);  sb_printf(&bas, "extensions/%s/%s.bas", m, m);
        sb_init(&inst); sb_printf(&inst, "extensions/%s/install.sb", m);
        int ok = file_exists(so.p) && file_exists(lib.p) && file_exists(bas.p) && file_exists(inst.p);
        sb_free(&so); sb_free(&lib); sb_free(&bas); sb_free(&inst);
        if (ok) {
            fprintf(F, "cd extensions/%s\n", m);
            fprintf(F, "%s/scriba -n install.sb\n", BIN);
            fputs("cd ../..\n", F);
        }
    }
    free_list(modules, n);

    fputs("echo \"copy the scriba library file\"\n", F);
    s = ius_CopyFile("./bin/lib/libscriba.a", strf("%s/libscriba.a", LIB), "root", "444");
    fputs(s, F); free(s);
    fputs("echo \"copy the example preprocessor to the source directory\"\n", F);
    s = ius_CopyFile("./heber.bas", strf("%s/heber.bas", SOURCE), "root", "444");
    fputs(s, F); free(s);

    fputs_interp(HD_INSTALL_SH_TAIL, F);
    fclose(F);
    chmod_path("install.sh", 0500);

    /* etc-init.d-sbhttpd */
    F = fopen("etc-init.d-sbhttpd", "wb");
    if (!F) die_at(1394, "Can not write etc-init.d-sbhttpd");
    fputs_interp(HD_INITD_SBHTTPD, F);
    fclose(F);

    /* config.pl */
    F = fopen("config.pl", "wb");
    if (!F) die_at(1513, "Can not write config.pl");
    fputs_interp(HD_CONFIG_PL, F);
    fclose(F);

    /* mkdeb.sh */
    F = fopen("mkdeb.sh", "wb");
    if (!F) die_at(1534, "Can not write mkdeb.sh");
    fputs_interp(HD_MKDEB_SH, F);
    fclose(F);
    chmod_path("mkdeb.sh", 0500);

    /* mkrpm.sh */
    F = fopen("mkrpm.sh", "wb");
    if (!F) die_at(1618, "Can not write mkrpm.sh");
    fputs_interp(HD_MKRPM_SH, F);
    fclose(F);
    chmod_path("mkrpm.sh", 0500);

    print_msg("Now I will try to determine the largest safe value for maxlevel.\n");
    print_msg("This may take a while, stay tuned...\n");

    if (!write_file_mode("tmpconf.lsp", "maxlevel 0\n", 0))
        die_at(1703, "Can not write tmpconf.lsp");
    run_quiet("./bin/exe/scriba -f tmpconf.conf -k tmpconf.lsp 2> /dev/null");
    run_quiet("./bin/exe/scriba -n -f tmpconf.conf infrec.sb");
    char *maxlevel = read_first_line_raw("depth.txt");
    if (!maxlevel) die_at(1710, "there was not depth.txt created");
    MAXLEVEL = (int)strlen(maxlevel);
    free(maxlevel);
    print_msg("Maxlevel is %d\n", MAXLEVEL);
    unlink_path("tmpconf.conf");
    unlink_path("tmpconf.lsp");
    unlink_path("depth.txt");

    if (MAXLEVEL < 100) die_at(1719, "Call stack is too low in the compiled scriba executable");
    MAXLEVEL -= 100;
    {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "%d", MAXLEVEL);
        subst_set("MAXLEVEL", tmp);
    }

    get_config_file();

    F = fopen("scriba.conf.unix.lsp", "wb");
    if (!F) die_at(1724, "Cannot write scriba.conf.unix.lsp");
    fputs_interp(HD_SCRIBA_CONF, F);
    fclose(F);
}

static void get_config_file(void)
{
    /* the template is assigned to $SCRIBA_CONF_UNIX_LSP and written by the
     * caller; nothing else happens here */
}

/* ------------------------------------------------------------------- help -- */

static void help_main(void)
{
    int counter = 0;

    if (!lastOptionWasHelp) {
        for (int i = 0; i < nOpt; i++) {            /* the original: keys %OPT */
            const char *name = Opt[i].name;
            if (strcmp(name, "--help") == 0) continue;

            const char *text = help_for(name);
            char *neg = NULL;
            if (!text) {
                neg = negopt(name);
                text = help_for(neg);
            }
            char *interpolated = NULL;
            if (!text) {
                subst_set("_", name);
                interpolated = interp(HD_HELP_INVALID);
                text = interpolated;
            }

            /* $helptext =~ s/\#\#\#.*$// : drop the rest of the ### line */
            SB t; sb_init(&t);
            const char *cut = strstr(text, "###");
            if (cut) {
                size_t keep = (size_t)(cut - text);
                sb_need(&t, keep);
                memcpy(t.p, text, keep);
                t.len = keep;
                t.p[keep] = '\0';
                const char *nl = strchr(cut, '\n');
                if (nl) sb_puts(&t, nl);
            } else {
                sb_puts(&t, text);
            }
            print_msg("%s\n", t.p);
            sb_free(&t);
            free(interpolated);
            free(neg);
            counter++;
        }
    }

    if (counter == 0) {
        fputs(HD_HELP_USAGE, stdout);
        /* sorted keys %HELP */
        int *idx = xmalloc((size_t)(SETUP_HELP_COUNT ? SETUP_HELP_COUNT : 1) * sizeof *idx);
        for (int i = 0; i < SETUP_HELP_COUNT; i++) idx[i] = i;
        for (int i = 1; i < SETUP_HELP_COUNT; i++) {
            int k = idx[i], j = i - 1;
            while (j >= 0 && strcmp(SETUP_HELP[idx[j]].name, SETUP_HELP[k].name) > 0) { idx[j + 1] = idx[j]; j--; }
            idx[j + 1] = k;
        }
        for (int i = 0; i < SETUP_HELP_COUNT; i++)
            print_msg("    %s\n", SETUP_HELP[idx[i]].name);
        free(idx);
        fputs(HD_HELP_TAIL, stdout);
    }
}

/* -------------------------------------------------------------- self check - */
/* The original reads setup.pl and checks that every option the program uses is
 * documented in %HELP.  The C port reads its own source the same way. */

static void self_check(void)
{
    FILE *f = fopen("tools/codegen/setup.c", "rb");
    if (!f) die_at(2855, "Can not open setup.c");
    SB src; sb_init(&src);
    {
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            sb_need(&src, n);
            memcpy(src.p + src.len, buf, n);
            src.len += n;
            src.p[src.len] = '\0';
        }
    }
    fclose(f);

    int counter = 0;
    for (size_t i = 0; i + 3 < src.len; i++) {
        if (strncmp(src.p + i, "opt", 3) != 0) continue;
        if (i > 0 && (isalnum((unsigned char)src.p[i - 1]) || src.p[i - 1] == '_')) continue;
        size_t j = i + 3;
        while (isalnum((unsigned char)src.p[j]) || src.p[j] == '_') j++;
        while (src.p[j] == ' ' || src.p[j] == '\t' || src.p[j] == '\n') j++;
        if (src.p[j] != '(') continue;
        j++;
        while (src.p[j] == ' ' || src.p[j] == '\t' || src.p[j] == '\n') j++;
        if (src.p[j] != '"') continue;
        j++;
        size_t start = j;
        while (j < src.len && src.p[j] != '"') j++;
        if (j >= src.len) break;
        char *option = xmemdup(src.p + start, j - start);
        i = j;
        if (strncmp(option, "--", 2) != 0) { free(option); continue; }
        if (strcmp(option, "--help") == 0) { free(option); continue; }
        const char *h = help_for(option);
        if (!h) {
            char *neg = negopt(option);
            h = help_for(neg);
            free(neg);
        }
        if (!h) {
            counter++;
            print_msg("Option '%s' is used, but no help: this is an internal error\n", option);
        }
        free(option);
    }
    sb_free(&src);

    if (counter == 0)
        print_msg("setup.c seems to be OK, at least for the option help text.\n");
}
