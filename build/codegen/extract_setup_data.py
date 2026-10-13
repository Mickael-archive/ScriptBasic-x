#!/usr/bin/env python3
"""Regenerate build/codegen/setup_data.h and build/codegen/setup_text.h.

The C port of setup.pl needs the data the Perl original carries inline: the
option help texts (%HELP), the module skeleton files (%SKELETONS), the
installation directory defaults and the shell/config templates it prints.
Extracting them mechanically keeps the port byte identical with the original.
Run from the repository root:

    python3 build/codegen/extract_setup_data.py
"""
import re
import sys

src = open('setup.pl', 'rb').read().decode('latin-1')
LINES = src.split('\n')

ENTRY = re.compile(r"\s*'((?:[^'\\]|\\.)*)'\s*=>\s*<<('?)([A-Za-z_]+)(?:'?,?)\r?$")
HD = re.compile(r"<<('?)([A-Za-z_]+)'?[;,]?$")


def parse_hash(start_pat):
    """Return (entries, first_line, last_line).  Heredoc bodies are consumed
    whole so that their parentheses cannot confuse the scan."""
    m = re.search(start_pat, src)
    if not m:
        sys.exit('not found: ' + start_pat)
    first = src[:m.end()].count('\n') + 1
    i = first                       # 0-based index of the line after the "("
    out = []
    while i < len(LINES):
        line = LINES[i]
        if line.startswith('  );') or line.startswith(');'):
            break
        m2 = ENTRY.match(line)
        if m2:
            key = m2.group(1).replace("\\'", "'")
            term = m2.group(3)
            body = []
            i += 1
            while i < len(LINES) and LINES[i].rstrip('\r') != term:
                body.append(LINES[i].rstrip('\r'))
                i += 1
            out.append((key, '\n'.join(body) + '\n', m2.group(2) == "'"))
            i += 1
            continue
        i += 1
    return out, first, i + 1


def collect_heredocs():
    """(opener_line, terminator_quoted, body) for every heredoc in the file."""
    out = []
    i = 0
    while i < len(LINES):
        line = LINES[i].rstrip('\r')
        m = HD.search(line)
        if m:
            term = m.group(2)
            body = []
            opener = i + 1
            i += 1
            while i < len(LINES) and LINES[i].rstrip('\r') != term:
                body.append(LINES[i].rstrip('\r'))
                i += 1
            out.append((opener, m.group(1) == "'", '\n'.join(body) + '\n'))
            i += 1
            continue
        i += 1
    return out


help_entries, help_from, help_to = parse_hash(r'%HELP\s*=\s*\(')
skel_entries, skel_from, skel_to = parse_hash(r'%SKELETONS\s*=\s*\(')

# installation defaults: the scalars and the hash that names them
scalars = dict(re.findall(r"^\$INSTALL_DEFAULT_(\w+)\s*=\s*'([^']*)';", src, re.M))
inst_vals = {}
for m in re.finditer(r"'(--install[\w-]*)'\s*=>\s*\$INSTALL_DEFAULT_(\w+),", src):
    inst_vals[m.group(1)] = scalars[m.group(2)]

if not help_entries or not skel_entries or not inst_vals:
    sys.exit('extraction failed: help=%d skel=%d inst=%d'
             % (len(help_entries), len(skel_entries), len(inst_vals)))


def interp_help(text):
    """%HELP heredocs interpolate; the INSTALL_DEFAULT scalars are the only
    variables that appear in them, in both the $NAME and ${NAME} forms."""
    def repl(m):
        return scalars.get(m.group(1), m.group(0))
    text = re.sub(r'\$\{INSTALL_DEFAULT_(\w+)\}', repl, text)
    text = re.sub(r'\$INSTALL_DEFAULT_(\w+)', repl, text)
    return text


def cstr(b):
    out = []
    for ch in b:
        o = ord(ch)
        if ch == '\n':
            out.append('\\n')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif o == 9:
            out.append('\\t')
        elif 32 <= o < 127:
            out.append(ch)
        else:
            out.append('\\x%02x' % o)
    return '"' + ''.join(out) + '"'


# --------------------------------------------------------------------- tables
w = []
w.append('/* Generated from setup.pl by build/codegen/extract_setup_data.py -- do')
w.append(' * not edit.  The option help texts (%HELP), the module skeleton files')
w.append(' * (%SKELETONS) and the installation directory defaults, byte for byte as')
w.append(' * the Perl original carries them. */')
w.append('')
w.append('#ifndef SETUP_DATA_H')
w.append('#define SETUP_DATA_H')
w.append('')
w.append('typedef struct { const char *name; const char *text; } SetupText;')
w.append('')
w.append('static const SetupText SETUP_HELP[] = {')
for k, v, _ in help_entries:
    w.append('    { %s,\n      %s },' % (cstr(k), cstr(interp_help(v))))
w.append('};')
w.append('#define SETUP_HELP_COUNT ((int)(sizeof SETUP_HELP / sizeof SETUP_HELP[0]))')
w.append('')
w.append('static const SetupText SETUP_SKELETONS[] = {')
for k, v, _ in skel_entries:
    w.append('    { %s,\n      %s },' % (cstr(k), cstr(v)))
w.append('};')
w.append('#define SETUP_SKELETON_COUNT ((int)(sizeof SETUP_SKELETONS / sizeof SETUP_SKELETONS[0]))')
w.append('')
w.append('static const SetupText SETUP_INSTALL_DEFAULT[] = {')
for k in sorted(inst_vals):
    w.append('    { %s, %s },' % (cstr(k), cstr(inst_vals[k])))
w.append('};')
w.append('#define SETUP_INSTALL_DEFAULT_COUNT ((int)(sizeof SETUP_INSTALL_DEFAULT / sizeof SETUP_INSTALL_DEFAULT[0]))')
w.append('')
w.append('#endif /* SETUP_DATA_H */')
open('build/codegen/setup_data.h', 'w').write('\n'.join(w) + '\n')

# ------------------------------------------------------------------ templates
t = []
t.append('/* Generated from setup.pl by build/codegen/extract_setup_data.py -- do')
t.append(' * not edit.  Every shell script and configuration template setup.pl prints,')
t.append(' * verbatim, with the backslash escapes the Perl source needs kept as they')
t.append(' * are.  Run time interpolation ($VAR, ${VAR} and \\$ for a literal dollar)')
t.append(' * is done by setup_interp() in setup.c. */')
t.append('')
t.append('#ifndef SETUP_TEXT_H')
t.append('#define SETUP_TEXT_H')
t.append('')
t.append('static const char *SETUP_HD[] = {')
manifest = []
n = 0
for line, quoted, body in collect_heredocs():
    if help_from - 1 <= line <= help_to or skel_from - 1 <= line <= skel_to:
        continue                      # already in the tables above
    n += 1
    first = body.split('\n')[0]
    t.append('    /* SETUP_HD_%d: setup.pl line %d%s */' %
             (n, line, ', interpolated at run time' if not quoted else ''))
    t.append('    %s,' % cstr(body))
    manifest.append((n, line, quoted, body))
t.append('};')
t.append('#define SETUP_HD_COUNT ((int)(sizeof SETUP_HD / sizeof SETUP_HD[0]))')
t.append('')
t.append('#endif /* SETUP_TEXT_H */')
open('build/codegen/setup_text.h', 'w').write('\n'.join(t) + '\n')

print('HELP %d, SKELETONS %d, install defaults %d, templates %d'
      % (len(help_entries), len(skel_entries), len(inst_vals), n))
for num, line, quoted, body in manifest:
    vs = sorted(set(re.findall(r'(?<!\\)\$([A-Za-z_][A-Za-z_0-9]*)', body)))
    print('SETUP_HD_%-3d setup.pl:%-5d %-4d lines  %s%s'
          % (num, line, body.count('\n') + 1,
             body.split('\n')[0][:44],
             '   vars: ' + ' '.join(vs) if vs else ''))
