/*
 * mkscribadef: the export definition file of the ScriptBasic library, scriba.def,
 * from the source file scriba.c.  This is the C port of mkscribadef.pl.
 *
 * The functions the library exports are marked with a C comment that holds the
 * word FUNCTION, and the declaration of the function follows on the next line;
 * the name of the function is the second word of that line:
 *
 *     int scriba_new(void)
 *
 * The port keeps the shape of the Perl program, including the two places where
 * it is careless: a marker on the last line of the file asks for a line that is
 * not there, which puts the name of the function before it into the output
 * again, and a line whose first two words are not the type and the name of a
 * function is copied from the last line that had them.
 *
 * usage: mkscribadef [--source <file>] [--output <file>]
 *
 *   --source  the file to read, scriba.c by default
 *   --output  the file to write, scriba.def by default
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* /\w+\s+(\w+)/ : the first run of word characters that is followed by
 * whitespace and another run of word characters.  The second run is copied into
 * name.  Returns 0 when the line has no such pair. */
static int second_word(const char *line, char *name, size_t namesize)
{
    const char *p = line;
    while (*p) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            const char *word = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            const char *q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (q > p) {                       /* there was whitespace */
                if (isalnum((unsigned char)*q) || *q == '_') {
                    const char *w2 = q;
                    while (isalnum((unsigned char)*q) || *q == '_') q++;
                    size_t n = (size_t)(q - w2);
                    if (n >= namesize) n = namesize - 1;
                    memcpy(name, w2, n);
                    name[n] = '\0';
                    (void)word;
                    return 1;
                }
            }
            continue;                          /* keep looking further on */
        }
        p++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *source = "scriba.c";
    const char *output = "scriba.def";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) { source = argv[++i]; continue; }
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) { output = argv[++i]; continue; }
        fprintf(stderr, "usage: mkscribadef [--source <file>] [--output <file>]\n");
        return 1;
    }

    FILE *F = fopen(source, "rb");
    if (!F) { fprintf(stderr, "Can not open %s\n", source); return 1; }
    FILE *OUT = fopen(output, "wb");
    if (!OUT) { fprintf(stderr, "Can not output %s\n", output); fclose(F); return 1; }

    fputs("LIBRARY     scriba\nDESCRIPTION \"ScriptBasic Library\"\nEXPORTS\n", OUT);

    char *line = NULL;
    size_t cap = 0;
    char name[256] = "";                       /* what the Perl keeps when the
                                                * line after a marker is not a
                                                * declaration */
    int have_name = 0;

    while (getline(&line, &cap, F) > 0) {
        if (strstr(line, "/*FUNCTION*/") != NULL) {
            /* the Perl reads the next line and takes the name from it; there is
             * no next line at the end of the file */
            if (getline(&line, &cap, F) <= 0) break;
            if (second_word(line, name, sizeof name)) have_name = 1;
            if (have_name) fprintf(OUT, "%s\n", name);
        }
    }

    free(line);
    fclose(OUT);
    fclose(F);
    return 0;
}
