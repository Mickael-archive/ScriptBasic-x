#!/usr/bin/env python3
"""Regenerate build/codegen/t2h_text.h from t2h.pl.

The C port of t2h.pl (build/codegen/t2h.c) prints the same html the Perl
original does.  The Perl script builds that html out of a handful of heredocs;
this program lifts them out of t2h.pl mechanically so the port cannot drift
from the original.  Every `$variable' interpolation of a heredoc becomes a %s
and the variable names are recorded in the comment above the template, in the
order the format expects them.

The generated header is committed, so the build does not need Python.

Run from the repository root:

    python3 build/codegen/extract_t2h_templates.py
"""
import re

SRC = open('t2h.pl', encoding='latin-1').read().replace('\r\n', '\n')
LINES = SRC.split('\n')

ASSIGN = re.compile(r'^\$(\w+)\s*=\s*<<\s*(\w+)\s*;?\s*$')
PRINT = re.compile(r'^\s*(?:print\s+(\w+)\s+)?<<\s*(\w+)\s*;?\s*$')


def take_heredoc(first, tag):
    """The body lines of the heredoc that starts at LINES[first]."""
    body = []
    i = first + 1
    while i < len(LINES) and LINES[i].rstrip() != tag:
        body.append(LINES[i])
        i += 1
    return body, i + 1


def c_template(body):
    """(C literal, interpolated variable names) for a heredoc body."""
    text = '\n'.join(body) + '\n'      # the newline that ends the last line
    text = text.replace('%', '%%')     # literal percent signs first
    text = text.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t')
    names = []

    def subst(m):
        names.append(m.group(1) or m.group(2))
        return '\x01'

    text = re.sub(r'\$\{(\w+)\}|\$(\w+)', subst, text)
    text = text.replace('\x01', '%s').replace('\n', '\\n"\n    "')
    return text, names


def main():
    defs, prints = [], []
    i = 0
    while i < len(LINES):
        m = ASSIGN.match(LINES[i])
        if m:
            start = i + 1
            body, i = take_heredoc(i, m.group(2))
            defs.append((m.group(1), start, body))
            continue
        m = PRINT.match(LINES[i])
        if m:
            start = i + 1
            body, i = take_heredoc(i, m.group(2))
            prints.append((m.group(1) or 'F', start, body))
            continue
        i += 1

    out = ['/*\n * t2h_text.h -- the output templates of t2h.pl\n *\n',
           ' * Generated from t2h.pl by build/codegen/extract_t2h_templates.py; every\n',
           " * `$variable' interpolation of a Perl heredoc became a %s and the variables\n",
           ' * appear in the order the format expects them (see the comment above each\n',
           ' * template).\n */\n']
    for name, line, body in defs:
        lit, _names = c_template(body)
        out.append('\n/* t2h.pl line %d: $%s (no interpolation) */\nstatic const char *%s =\n    "%s";\n'
                   % (line, name, name, lit))
    for k, (target, line, body) in enumerate(prints, 1):
        lit, names = c_template(body)
        out.append('\n/* t2h.pl line %d, print to %s; arguments: %s */\nstatic const char *T%02d =\n    "%s";\n'
                   % (line, target, ', '.join(names) or '(none)', k, lit))
    open('build/codegen/t2h_text.h', 'w').write(''.join(out))
    print('build/codegen/t2h_text.h written: %d templates' % (len(defs) + len(prints)))


if __name__ == '__main__':
    main()
