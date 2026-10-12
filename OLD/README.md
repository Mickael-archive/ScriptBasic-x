# OLD

Archived files that are no longer part of the build or the maintained source
tree. They are kept here only for historical reference and are **not compiled,
generated from, or shipped**.

The directories that need explaining have a README of their own:

* `texi-module-pages/` - the 17 old `mod_*.texi.jam` module pages, superseded by
  `extensions/<module>/manual.texi.jam`, with the story of the `hash` page that
  was the one real loss;
* `upstream-finds/` - the copies of the module sources this tree had lost,
  recovered from the SourceForge release archives, with the archaeology and
  where each name went.  The release tarballs themselves are not committed; its
  README records their URLs and checksums.

## Perl code generators (replaced by the C tools in `tools/codegen/`)

The following Perl scripts used to generate C sources at build time. They have
been ported to portable C23 programs that produce byte-identical output and are
now built and invoked by the `setup` script / `setup.pl` / `makefile.jam` /
`Makefile.nt.jam`:

| Archived Perl script | C replacement               | Generates                              |
|----------------------|-----------------------------|----------------------------------------|
| `generrh.pl`         | `tools/codegen/generrh.c`   | `errcodes.c`, `include/error.bas`      |
| `headerer.pl`         | `tools/codegen/headerer.c`  | the `*.h` headers from the `*.c` files |
| `syntaxer.pl`        | `tools/codegen/syntaxer.c`  | `syntax.c`, `notimp.c`                 |
| `lmt_make.pl`        | `tools/codegen/lmt_make.c`  | `lmt_*.c` from `lmt_*.def`             |

The Jamal macro processor is not a code generator and is therefore not
archived: it has been ported as well and is described in the next section.

The C replacements are functionally equivalent. Note that `syntaxer.pl` was not
deterministic (it iterated a Perl hash), so `syntaxer.c` additionally fixes the
output order to be reproducible.

`headerer.bas`, the ScriptBasic re-implementation of `headerer.pl` that shipped
for the bootstrap case, is superseded by the same C tool and is archived here as
well.

## The Jamal preprocessor

`tools/codegen/jamal.c` is a full port of the macro processor. `setup` builds it
with the other tools and runs it to generate `bin/make/Makefile` and the
`extensions/*/makefile` files, so the **compilation flow needs no Perl at all**.
Every file in the tree that contains Jamal macros (104 of them: `makefile.jam`,
`*.jim`, the `extensions/*/makefile.jam` module makefiles and the whole
`texi/*.texi.jam` documentation set) and 34 additional stress files were compared
against the Perl original: same stdout byte for byte, same exit status.

`jamal.pl` stays in the tree as the reference implementation. The build uses
`bin/tools/jamal`; `make_common.jim` declares `JAMAL` and `T2H` with `{#define?}`,
and `setup` passes the Perl spelling of them on the command line only when the
binaries have not been built, so such a tree falls back to the Perl originals
instead of failing.

Known differences from the Perl original:

| Feature | Behaviour |
|---------|-----------|
| Perl extension modules (`{#use x}`, `{#base::macro ...}`) | not available; reported as an error like the original does for a module that cannot be loaded. The `esd` module the documentation uses **is** built in, see the next section |
| `{#with pkg}` | the original stores an array reference where a scalar is expected and dies at run time for every reachable input; this port switches the base module instead |
| `{#dir ...}` | not implemented (unused anywhere in the tree); reported as an error |
| division by zero in an operational macro | the original dies with `Illegal division by zero at jamal.pl line 912.` and status 2; the same text and status are printed |

One detail worth knowing: the original skips only spaces and newlines after the
macro delimiter, because it compares a bareword `char` against tab and carriage
return in a way that can never match. The port does the same.

## The documentation chain: `esd` and `t2h`

The texi sources pull the documented source files in through the `esd` Jamal
extension module (`esd.pm`): `{#esd::command n s{...}{...}}` registers a
substitution that is then applied to every line of what `{#esd::include file}`
reads. `tools/codegen/jamal.c` implements that module, so `{#use esd}` works
without a Perl interpreter, and `tools/codegen/t2h.c` is a port of `t2h.pl`, the
texi to html converter that turns `bin/texi/mod_*.texi` into the html pages. The
module makefiles run both of them, so `./setup --unix` now builds the code and
the documentation without invoking Perl.

Evidence, against the Perl originals on the same input:

| Check | Result |
|-------|--------|
| the 47 `*.texi.jam` inputs (26 under `texi/`, `esd.texi.jam`, the 20 module `manual.texi.jam`): `perl jamal.pl` vs `bin/tools/jamal` | output file, stdout and exit status identical, byte for byte |
| the `esd` substitution engine: every `esd::command` of the tree, applied to 4 516 lines of the real documentation | 126 448 substitutions, all identical |
| the 47 documents produced above through `perl t2h.pl` vs `bin/tools/t2h` | 2 225 of the 2 226 output files identical |
| a whole `./setup --unix` build of the tree, once driven by `bin/tools/setup` and once by `perl setup.pl` | 1 020 files on either side, all identical except `scriba`, `scribacmd.o` and `setup.log`, which carry the compilation time |

Differences, all of them places where the Perl original depends on the
unordered iteration of a hash:

| Feature | Behaviour |
|---------|-----------|
| order of the `esd::command` substitutions | `esd.pm` keeps them in a hash and applies them in `keys %COMMAND` order, which is a different order on every run of the same Perl; this port applies them sorted by key, the order the shipped `*.texi.jam` files are written for |
| order of the `.hhk` index entries | `t2h.pl` writes them in hash order too; the port writes them sorted. This is the single file of the 2 226 that is not byte identical, and the original cannot even be compared with itself: two runs on the same input produce two different files |
| Perl regular expressions in `{#esd::command ...}` | only the subset the shipped files use is understood -- literals, escapes, `.`, `^`, `$`, character classes, groups, lazy quantifiers, the `g` flag. Anything else is reported as an unsupported command instead of being silently mis-applied |

### The module manual pages

`extensions/*/manual.texi.jam` (16 of the 20) asked for
`../../html/texi/skeleton.jim`, a file that exists neither in the tree nor in its
history, so those pages came out empty; `perl jamal.pl` printed the same "cannot
be opened" message and wrote the same empty file. The one skeleton the tree has is
`texi/skeleton.jim`: it is what `texi/mod_*.texi.jam` include, and what `setup.pl`
copies into a module it creates. The 16 includes, and the two places that generate
them (`setup.pl` and the `%SKELETONS` table of `tools/codegen/setup_data.h`), now
name `../../texi/skeleton.jim`.

That alone would not have been enough. `esd::include` reads the module source,
keeps the lines between its two markers and compares those markers with a plain
`eq` after `chomp`; the `extensions/*/interface.c` files are CRLF, and `chomp`
takes off the LF only. The marker line is `"/**\r"`, `eq "/**"` is false for it,
and the whole file is thrown away: `esd.pm` only ever worked where Perl reads its
files in text mode, on Windows. `esd.pm` and its port now both drop the CR, so the
reader behaves the same on either platform.

Measured on the 47 documents, one side at a time: 14 of them change, all from an
empty page to the real one -- `esd.texi.jam` 44 bytes to 49 744, and the 13 module
manuals whose source has a `/**` block (`curl` 455 to 46 683, `cgi` 452 to
19 937, `mt` 449 to 12 413, ...). The three modules whose sources carry no such
block (`hash`, `psql`, `trial`) are unchanged, and so are the other 33 documents.
`./setup --unix` now writes the real pages into `bin/texi/mod_*.texi`, and the
html pages next to them.

Fixing that exposed a defect in the `t2h` port that the empty pages had hidden:
the port kept one empty field where Perl's `split /\n/` drops the empty fields at
the end of a list, so every page of a document whose body ends on a blank line was
one byte longer (the `<HR>` separator pushed onto a line of its own). It is fixed,
and the two implementations agree again on every page.

One documentation path is still broken, deliberately untouched: `texi/mod_*.texi.jam`
are not what `make` builds (that is `extensions/*/manual.texi.jam`), they are the
input of the separate `mkth.sh` / `mkath.cmd` run, and 13 of the 17 name a source
that is not in the tree (`extensions/cgi/cginterf.c`, `extensions/ux/ux.c`,
`extensions/zlib/zlbintrf.c`, ...).  `md5` and `tools` are modules that no longer
exist at all, the other four (`bdb`, `gd`, `hash`, `mysql`) carry their text
instead of an include.  Repointing the eleven whose module still exists is
mechanical, the two orphans need a decision.

## The setup program

`tools/codegen/setup.c` is the C port of `setup.pl`. The UNIX entry point is the
shell script `setup` in the top directory:

    ./setup

which builds the tools in `tools/codegen` and then runs `bin/tools/setup`
(`make` and `make install` call the same script). The Perl driver `setup.pl` is
still in the tree and still works (`perl setup.pl --unix`), but it is no longer
needed: it drives the same C tools. The port was checked against the Perl
original by building the whole tree twice from the same clean checkout, once with
each driver:

| Check | Result |
|-------|--------|
| complete build | both exit with status 0 |
| every file of the two trees (813 of them, with the `--install` case run too) | identical, once the directory the tree was built in is normalised |
| the two generated binaries (`scriba`, `scribacmd.o`) | differ only in the compilation time the compiler stamps into them |
| `setup.log`, the ordered log of every command that ran | identical, line for line, timestamps apart |
| the same build with the output going to a terminal | identical, line for line |
| `--help`, `--check`, `--self-check`, `--module=hash`, `--module=<new module>`, `--unix --nt`, `--badoption`, `--no-tools --unix --no-compile`, `--install --no-install-interactive` | identical stdout and exit status |
| the skeleton files of a brand new module | identical |
| `install.sh`, `mkrpm.sh`, `mkdeb.sh`, `etc-init.d-sbhttpd`, `scriba.conf.unix.lsp`, `compile` | identical |

The one value that cannot be reproduced is the `maxlevel` number `--install`
probes for: the original runs `scriba` into an endless recursion and takes the
length of the line the crashed interpreter left in `depth.txt`. That length
varies by some 0.1% from run to run on the same machine, so
`scriba.conf.unix.lsp` reports a slightly different number every time, whichever
driver is used; everything else in the file is identical.

Two places where the original depends on the order of a Perl hash (which Perl
randomises in every process) iterate the table in order here, which makes the
output reproducible: the option named in the "Invalid option" message, and the
order of the interactive `--install` questions.

The data the original carries inline - the `%HELP` texts, the `%SKELETONS`
module files, the installation defaults and every shell and configuration
template - lives in `tools/codegen/setup_data.h` and
`tools/codegen/setup_text.h`. Both are checked in, so the build needs no Python;
`tools/codegen/extract_setup_data.py` regenerates them from `setup.pl`, and has
to be run again only when the texts change there. It had one trap: it emits the
`SETUP_HD` entries in the order they appear in `setup.pl`, while `setup.c` names
them by index, and the table that was checked in followed the order `setup.c`
expects, so re-running the command silently renumbered the messages. The entries
are named by the current order now, which makes the command idempotent again.

The shell script `setup` had two defects of its own, fixed along the way: it was
not executable (mode 644, so `make all` could not run it) and its CRLF line
endings made the `#! /bin/sh` line unusable on UNIX.


## `-fPIC` for the module shared objects (`CcOptPIC`)

The module makefiles compiled their objects without `-fPIC` and then linked the
shared object with `ld -shared`, which the current GNU linker refuses
(`relocation R_X86_64_PC32 ... recompile with -fPIC`). That is why `cgi`, `dbg`
and `trial` did not build. `make_gcc.jim` and `make_darwin.jim` now define
`{#define CcOptPIC=-fPIC}` (empty in `make_bcc.jim` and `make_vc7.jim`, where the
flag does not exist), and the `{cc}` command of every `extensions/*/makefile.jam`
uses it in front of `{CcOptCompile}`. Those three modules now build their
objects, their `.so` and their `.a`; the other modules that still fail need
their optional libraries installed (libxml2, zlib, curl, a regex library,
mysql, gd, ncurses, odbc, postgres, berkeley db).

## Legacy makefiles

| Archived file                | Superseded by                                        |
|------------------------------|------------------------------------------------------|
| `Makefile.old`               | `makefile.jam` (processed by Jamal into `bin/make/Makefile`) |
| `Makefile.solaris`           | `makefile.jam` + the UNIX/GCC Jamal macro package    |
| `Makefile.sample.pgsql`      | the pgsql module makefile generated by `setup.pl`     |

## Dead, unreferenced scripts

Nothing in the tree invokes these; they were left over from older releases,
partial refactorings and one-off experiments. They are kept only for reference.

| Archived file               | What it was                                              |
|-----------------------------|----------------------------------------------------------|
| `esdcnv.pl`                 | `.esd` conversion helper (unused)                        |
| `esd2texi.pl`               | `.esd` to Texinfo converter (superseded by `esd2html.pl`)|
| `esd2any.pl`                | generic `.esd` conversion driver (unused)                |
| `statltmaker.pl`            | static-table/statistics generator stub (unused)          |
| `moduletablemaker.pl`       | module table generator (unused; `lmt_make` is the live one) |
| `lmt_help.pl`               | `lmt_*.def` helper (unused; `lmt_make.pl` was the live one) |
| `test/mktest.pl`            | one-off generator of `stresstest.bas` (never wired in)   |
| `examples/mktest.pl`        | one-off generator of `concat.txt` from `*.bas` (unused)  |

## Frozen copy of the old HTML/web build harness (`OLD/html/`)

`html/` holds the source of the project web site (Jamal + ssplit), but the live
rebuild flows (`mkweb.cmd`, `mksamples.cmd`, `mkth*.cmd`) always run the
**top-level** `jamal.pl`, `ssplit.pl` and `mkweb.pl`. The copies that used to sit
*inside* `html/`, `html/examples/` and `html/embdguid/` were frozen snapshots of
that tooling together with their own tiny driver scripts; nothing referenced them
anymore, so they are archived here:

| Archived file                          | Notes                                   |
|----------------------------------------|-----------------------------------------|
| `html/jamal.pl` (and the copies under `html/examples/`, `html/embdguid/`) | older snapshots of the top-level Jamal |
| `html/make.pl`, `html/ssplit.pl` (and the copies under `html/examples/`, `html/embdguid/`) | web-build helpers, superseded by `ssplit.pl`/`mkweb.pl` |
| `html/make.cmd`, `html/make.sh`, `html/umake` | drivers of those local copies     |
| `html/examples/make.cmd`, `html/examples/build.cmd` | same, for the examples sub-site |
| `html/embdguid/make.cmd`, `html/embdguid/build.cmd` | same, for the embedded guide   |

The web **content** stays in place: `html/source.txt`, `html/examples/source.ssplit`,
`html/examples/progs/*.bas` and the `*.ssplit` inputs are untouched.

## Build files that still call Perl

Only code that has not been ported yet, or historical documents:

* `setup.pl` is the Perl driver of the build. It is no longer the entry point
  (`./setup` runs the C driver, see above) and the build does not need it, but it
  still works and drives the same C tools, so it is kept next to the C port.
* the reference originals `jamal.pl`, `t2h.pl` and `esd.pm` (the build uses the
  C ports, see above), and the rest of the documentation and packaging chain:
  `esd2html.pl`, `esd2texi.pl`, `esd2any.pl`, `esdcnv.pl`, `ssplit.pl`,
  `mkweb.pl`, `mkcdoc.pl`, `mkscribadef.pl`, `moduletablemaker.pl`,
  `statltmaker.pl`, `lmt_help.pl`, `preparedeb.pl`, `heber.pl`, `convert.pl`,
  `scriba-rpm.pl`, `scriba-tar.pl`, `scriba-zip.pl`.
* The `.cmd` command files that drive them, `mkth*.cmd`, `mktexihtml.cmd`,
  `mkweb.cmd`, `mkcdoc.cmd`, `mksamples.cmd`, `modconfigure.cmd`, `setup.cmd`.
* `Makefile.nt.jam` no longer uses Perl for the code generators, but it still
  runs `perl esd2html.pl` (source documentation) and `perl mkscribadef.pl`
  (the export definition file), neither of which is ported yet.
* `html/source.txt` (historical web source) still names `syntaxer.pl`.
* `setup.log` is a committed build log from 2013, before the C tools existed.
