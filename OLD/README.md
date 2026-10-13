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
| Perl extension modules (`{#use x}`, `{#base::macro ...}`) | not available; reported as an error like the original does for a module that cannot be loaded. The two modules the documentation uses, `esd` and `samples`, **are** built in, see the next sections |
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
| the 30 `*.texi.jam` inputs of the tree (the 9 of `texi/`, `esd.texi.jam`, the 20 module `manual.texi.jam`): `perl jamal.pl` vs `bin/tools/jamal` | identical byte for byte, output and exit status, for every one of them that both sides can build here: the 7 guides the Perl can still compare, `esd.texi.jam` and the 7 module pages this machine has the libraries for. `devguide` and `auxfiles` differ, both for the first reason in the table below (`eszterengine` joins them once `./setup --install` has written the init script it includes, see *The install files, and the two documents that need them*); `regtest` and `samplebas` are the two the C port does *more* for, see the `samples` section |
| the `esd` substitution engine: every `esd::command` of the tree, applied to 4 516 lines of the real documentation | 126 448 substitutions, all identical |
| `./mkth.sh all` twice, the second time with `perl` replaced by a script that refuses to run | exit 0, not one Perl invocation, the two runs byte identical, 3 036 files |
| the documents of `./mkth.sh all` through `perl t2h.pl` vs `bin/tools/t2h`, on the same `.texi` input | 3 035 of the 3 036 output files identical |
| a whole `./setup --unix` build of the tree, once driven by `bin/tools/setup` and once by `perl setup.pl` | 1 057 files on either side, all identical except `scriba`, `scribacmd.o` and `setup.log`, which carry the compilation time |

Differences, all of them places where the Perl original depends on the
unordered iteration of a hash:

| Feature | Behaviour |
|---------|-----------|
| order of the `esd::command` substitutions | `esd.pm` keeps them in a hash and applies them in `keys %COMMAND` order, which is a different order on every run of the same Perl; this port applies them sorted by key, the order the shipped `*.texi.jam` files are written for. `devguide` and `auxfiles` show what the old order costs: `texi/devguide.texi.jam` and `texi/auxfiles.texi.jam` both declare `0  s{\@\@}{SAVEALLDOUBLESOBAKA}g` (protect a doubled `@`) and a last command `X s{SAVEALLDOUBLESOBAKA}{\@\@}g` (put it back), so a `keys` order that runs `X` first leaves the internal marker in the document -- 282 of them in `auxfiles` -- and one that escapes in the wrong place writes broken texinfo (`@code@{...}` 862 times in `devguide`). The Perl output cannot be reproduced at all: four `PERL_HASH_SEED` values give three different `auxfiles.texi` |
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

### The `samples` extension module

`texi/regtest.texi.jam` and `texi/samplebas.texi.jam` are the two documents of the
regression suite: their sections show a sample program and then the output of
running it.  That is the second jamal extension module of the documentation, and
the tree carries its specification -- `texi/docudoc.texi.jam`, the guide that
describes how the documentation is written, under `@subsubsection samples.pm`:

> This jamal macro package defines two macros to include sample programs as well
> as their output into the code.  The macro `sample::sample` gets a sample
> program and saves it into a file.  The name of the file should be given by the
> first line of the argument.  The file saved can later be executed using the
> macro `sample::execute`.

`samples.pm` itself did not survive: it is not in the tree, not in the history of
the tree, and not in any of the release archives of `OLD/upstream-finds/`.  The
two macros are all the documents use, so both are built into the C jamal next to
`esd`: `{#use samples}` adopts the module as the base module, `samples::sample`
writes the file, and `samples::execute` is the same as `esd::execute`.  Where the
specification is silent the document decides:

* the text is expanded before it is written.  `regtest.texi.jam` has one sample
  (`onerror.bas`) whose text contains `[[[ERRMES]]]`, a macro the document defines
  just above it, and the prose next to it says the program prints what `ERRMES`
  stands for.  Expanding makes the file agree with the `@example` block that
  displays the same text: both read
  `print " An error has occured, but this is no problem."`.
* the file is written without a trailing newline, like the text it came from
  (`scriba` reads it either way).
* `samples::sample` returns nothing: the text around it in the document prints the
  program, so a value would print it a second time.

Both documents also needed a change and a build trick:

* `samplebas.texi.jam` had `SCRIBAEXE=T:\MyProjects\ScriptBasic\source\scriba.exe`
  compiled into it, a machine that does not exist.  It is `scriba` now, the one
  `./setup` builds into `bin/exe`, and `mkth.sh` puts `bin/exe` on the `PATH` for
  these two documents (and feeds them `/dev/null` on stdin, so a sample that asks
  for input sees end of file instead of waiting for the terminal); when
  `bin/exe/scriba` is missing it says that the results will come out empty.
* they were written for a current directory of `html/texi/tmp`: the lines of
  their `Result` templates run `scriba FILE 2> error.txt` -- which writes
  `error.txt` into the current directory -- and then include `tmp/error.txt`,
  which jamal resolves relative to the document.  `mkth.sh` makes `texi/tmp`, runs
  these two documents from there and removes it again: both names then point at
  the same file, and the sample programs (`regtest` writes 75 files, `samplebas`
  21) never reach the tree.

*Checked:*

| Check | Result |
|-------|--------|
| `./mkth.sh all` with a `perl` on the `PATH` that refuses to run | exit 0, not one Perl invocation; `regtest.texi` 83 853 bytes and `regtest.html` 103 656, `samplebas.texi` 27 888 and `samplebas.html` 35 108 (the size of these two moves by a few dozen bytes from build to build, see the last row) |
| the sample programs the documents write and run | `regtest`: 75 files in `texi/tmp` and 48 executions; `samplebas`: 21 files and 18 executions; the result sections carry what the interpreter printed (`hello1.bas` prints `HELLO!`, `replace.bas` prints `alabxmx mxma`, `asc.bas` prints `65` and `48`) |
| the version banner, included from `tmp/sversion.txt` after `scriba -v 2> sversion.txt` | `ScriptBasic v2.0` / `Variation >>STANDARD<< build 0` / `Magic value 859009842` / `Node size is 24` / `Extension interface version is 11` |
| the `Possible error messages sent to @code{stderr}` sections | six of them have content, the real `stderr` of the samples (`testnt.bas(1): error &H3e:Include file is not found` -- `nt.bas` is the Windows module) |
| against the Perl driver | it has no `samples.pm`, so it writes both documents with empty sample sections: given a `PATH` without `scriba` the port's output is byte identical to the Perl's except for the 49 + 1 lines the runs put into the `tmp/error.txt` sections it includes, and not one line of the Perl's output is missing from it.  With `scriba` the documents carry the output of the samples instead |
| reproducibility | these two documents cannot be byte-reproduced, and never could: they show the live output of the samples (`ARRAY@#B760B6B8`, `The local time is: 2026.10.12 22:38:31`).  70 lines differed between two runs here; nothing else in the chain varies |

### The guides, and `mkth.sh`

The 17 old `texi/mod_*.texi.jam` were the pages of the pre-`mkthm` flow: `make`
documents a module from `extensions/<module>/manual.texi.jam` into
`bin/texi/mod_<module>.texi`, and `mkath.cmd` converts that built page.  Thirteen
of the 17 named a source file that has not existed since the 1.0b30 rename, so
they are in `OLD/texi-module-pages/` now; the one real loss, the `hash` manual,
went back into the module.  What is left is the guides themselves, and that is
what `mkth.sh` builds.

`mkth.sh` was the UNIX half of the driver, written for a layout that no longer
exists: it did `cd html/texi`, made a `tmp/` under it and copied the results up
`../../../../`.  Nothing in it could run in this tree.  It now drives
`bin/tools/jamal` and `bin/tools/t2h` and writes `bin/texi/<name>.texi` plus the
html pages next to it, exactly where the module makefiles put `mod_*.texi`.
`./mkth.sh all` builds the ten documents of the tree, and it needs neither Perl
nor LaTeX.

Two include mechanisms meet in these files and they do not resolve a file name
the same way:

* `{#esd::include <file>}` opens `<file>` **relative to the current directory**
  (`esd.pm` passes it straight to `open`), and
* jamal's own `{#include <file>}` opens it **relative to the directory of the file
  that includes it**.

The documents were written for a current directory two levels below the source
root (`html/texi/tmp`), so they name their files `../../FILE_NAME`.  `texi/` is
one level below the root, so those names now take one step too many and the
guides asked for files outside the tree; `texi/auxfiles.texi.jam`,
`devguide.texi.jam`, `eszterengine.texi.jam`, `source.texi.jam` and
`ug.texi.jam` name `../FILE_NAME`, `../build.txt`, `../commands.texi` instead.
With jamal run from the document's own directory both mechanisms then look in
the source root, which is where the files are.  `docudoc.texi.jam` and
`prep_dbg.texi.jam` name no file and needed nothing.

One trap in the port was worth the detour: `t2h` takes the directory of its
output from its second argument.  Given a bare document name it writes into the
current directory instead, and the first version of the script filled the root of
the tree with 2 772 html pages.  It passes the full output path now, and
`bin/` is in `.gitignore`, as it is for the module pages.

Two of the ten documents include a file that is not in the tree and that only
`./setup --install` writes: `texi/auxfiles.texi.jam` includes `install.sh` and
`mkdeb.sh`, and `texi/eszterengine.texi.jam` includes `etc-init.d-sbhttpd`.  In a
tree that never ran the installation those two sections come out empty, and
`mkth.sh` names the file it is missing instead of quietly writing a short page.
Once `./setup --install` has run there is nothing to report: `./mkth.sh all` is
silent and the sections carry the text the include selects, which is what the
next section is about.

Two more were on this list and are not any more: the `ug` chapter was missing its
command reference chapter, which `bin/tools/mkcdoc` writes now, and
`regtest`/`samplebas` needed the `samples` extension module, which is built into
the C jamal (see below).  Both are part of `all`.

With all of that, the guides build: from a checkout of this commit,
`./mkth.sh all` gives 10 `.texi` documents and 3 903 html pages in `bin/texi` --
1 960 `.html` and 1 943 `.htm` -- including `ug.html` at 492 242 bytes,
`devguide.html` at 627 021 and `source.html` at 393 156, and nothing is missing
once `./setup --install` has written the files the two above need.

### The install files, and the two documents that need them

`./setup --install` is what writes the files a ScriptBasic installation is made
of: `install.sh` (the shell script that copies everything into place),
`mkdeb.sh` and `mkrpm.sh` (the two package scripts), the `etc-init.d-sbhttpd`
init script of the Eszter engine, `config.pl` and the configuration
`scriba.conf.unix.lsp`.  None of the six is in the repository: they carry the
installation directories of one machine, and the `--install` run probes the
largest `maxlevel` for it.  They are in `.gitignore` now, so the documented flow

    ./setup --unix
    ./setup --unix --install --no-install-interactive
    ./mkth.sh all

leaves no untracked file behind.  (The tracked `configure.jim`, `compile`,
`extensions/*/makefile` and the rest are the files `./setup` rewrites; the
*setup program* section below explains that habit.)

Two of the ten documents read those files: `auxfiles` includes `install.sh` and
`mkdeb.sh`, `eszterengine` includes `etc-init.d-sbhttpd`.  Without the
installation `mkth.sh` says which file is missing and the section is empty;
with it there is no warning and the section has text:

| File | Without `--install` | With `--install` |
|------|---------------------|------------------|
| `bin/texi/auxfiles.texi` | 42 748 bytes, 4 warning lines | 50 350 bytes, none |
| `bin/texi/eszterengine.texi` | 29 849 bytes, 2 warning lines | 30 225 bytes, none |
| `bin/texi/auxfiles.html` | 52 195 bytes | 59 858 bytes |
| `bin/texi/eszterengine.html` | 35 973 bytes | 36 344 bytes |

The include of both documents is `{#esd::include ../FILE}` with no start and no
stop string, and `esd::include` switches on at an empty line and off at the next
one.  That is the same mechanism that takes the lines between the `/**` markers
of a module source; here the marker is the empty line, so the sections carry the
runs of lines between blank lines, exactly as the Perl original did.  For
`etc-init.d-sbhttpd` that is the three assignments, the `$DAEMONIMAGE` check with
its `fi`, the `for`/`do` pair and the final `exit 0`.

*Checked:*

| Check | Result |
|-------|--------|
| `./setup --unix`, then `./setup --unix --install --no-install-interactive`, then `./mkth.sh all` | exit 0, not one Perl invocation, and **no warning at all** against the six warning lines of the same flow without `--install` |
| the two documents against the Perl with the three files in place | byte for byte what `perl jamal.pl` writes when the Perl iterates `%COMMAND` in sorted key order; a stock Perl iterates it in hash order and instead prints `@@$DAEMONIMAGE` where the sorted order (and texinfo) wants `@$DAEMONIMAGE` |
| the tree after the three commands | no untracked file; `git status` shows only the tracked files `./setup` rewrites |

The second row is the hash-order difference of the *documentation chain* section
above, in `eszterengine` this time: it could not show while the init script was
missing, because there was no `$` or `@` in the document to escape.  With the
file in place, `eszterengine` is the third document of the chain -- after
`devguide` and `auxfiles` -- whose Perl output cannot be reproduced run to run.

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

One habit of the original is worth knowing before running it in a git worktree:
`./setup --unix` rewrites the files it generates in the top directory and in
`extensions/*` -- `configure.jim` (with the absolute path of the tree),
`compile`, `modinst.sh`, `modmake.sh`, `setup.log`, `extensions/*/makefile`,
`extensions/*/libraries.jim` and `extensions/*/notunix.txt`.  Those files are
tracked, because the tree ships the ones the original setup wrote on the author's
machine (CRLF, `/Users/verhasp/github/ScriptBasic/ScriptBasic`), and `setup.pl`
did the same to them.  They show up as modified after a build; `git checkout --
<files>`, or building in a copy of the tree, puts the upstream copies back.


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
rebuild flows (`mkweb.cmd`, `mksamples.cmd`, `mkth*.cmd`) used to run the
**top-level** `jamal.pl`, `ssplit.pl` and `mkweb.pl`; they run the C tools of
`tools/codegen` now (see *The Windows command files and `ssplit`*). The copies
that used to sit
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

## The release packages, and the last Perl in them

The release of a build is made of these files, all named after the same
`scriba-<version>b<build>` (the `v` that the old scripts put in and left out
again from one artifact to the next is gone):

| Artifact | Made by |
| --- | --- |
| `scriba-2.0b0-source.tar.gz` | `./mktar.sh` |
| `scriba-2.0b0-source.zip`, `-bin.zip`, `-exe.zip` and the four documentation zips | `./setup --zip` and then `mkzip.cmd` under Windows with `wzzip` |
| `scriba-2.0b0-1_i386.deb` | `./mkdeb.sh` (written by `./setup --install`) |
| the RPM of the build | `./mkrpm.sh` (written by `./setup --install`) |

**`mktar.sh`** is the source package. `scriba-tar.pl` read the `CVS/Entries`
files and wrote a `./mktar` script that put exactly the files CVS knew about
into the tar, so that the tarball could not pick up compiled garbage; there are
no CVS directories in this tree, so `mktar.sh` takes that list from
`git ls-files`, which is the same set by another name. The archive holds the
files under `scriba-<version>b<build>/` and is reproducible: sorted members, no
owner, no group, no member time, and `gzip -n`, so the same tree gives the same
bytes every time (`8d6cbaf2...` twice in a row here). Two things of the Perl
original are fixed: `version.txt` and `build.txt` are CRLF and the Perl chomped
only the LF, so on UNIX it built a tar named `scriba-2.0
b0
-source.tar.gz`,
and `scriba-tar.pl` is kept as the reference original.

*Checked:* the 672 entries of the archive are exactly `git ls-files`; extracted
into a clean directory the release builds itself with `./setup --unix` and with
`./mkth.sh all` with a `perl` on the PATH that refuses to run, and the
interpreter of the extracted tarball runs a program.

**`./setup --zip`** no longer runs `perl scriba-zip.pl`. The C setup writes the
two files itself: `mkzip.lst`, the list of the tracked files with the directory
separator of Windows, and `mkzip.cmd`, the `wzzip` command lines. The old
script took the list from `CVS/Entries` too and died on a tree without them
("Can not open CV/Entries"); it also walked into the subdirectories with a
backslash in the path, which only works on Windows. The names of the seven zip
files were mixed up in the original (`scriba-v2.0b0-source.zip`,
`scriba-v2.0b0-bin.zip` and `scriba-v2.0b0-exe.zip` but `chm-v2.0b0-bin.zip`,
`text-v2.0b0-bin.zip`, `pdf-v2.0b0-bin.zip` and `html-v2.0b0-bin.zip`); all
seven carry the name of the build here. The `the file list of the source ZIP`
is the whole difference to look at: `mkzip.lst` has the same 672 names as
`git ls-files`. The zip files themselves need WinZip and Windows, which is what
`mkzip.cmd` is for, so nothing more can be checked here.

**`mkrpm.sh`** (generated by `./setup --install`) is written from a new
template. The old one deleted every generated file it could find - the
generated headers, `syntax.c`, `errcodes.c`, the whole `bin/` tree - before it
copied the directory into `scriba-<version>b<build>` and packed that, then ran
`perl scriba-rpm.pl` (which needed `config.pl` from the same `setup.pl` run) and
`rpm -ba /usr/src/redhat/SPECS/scriba-rpm.spec`. It builds the tar with
`./mktar.sh` now, so nothing has to be deleted and nothing outside the tracked
files can get in; it writes `scriba-rpm.spec` itself, with the installation
directories substituted by `setup.pl` when it generates the file; and it finds
the tree of `rpmbuild` in the `_topdir` macro, in `RPMTOPDIR`, or in the
`/usr/src/redhat` of the old redhat package manager. A directory it cannot
create is reported with a line that names `RPMTOPDIR`, instead of the
`Permission denied` of a `mkdir` in the middle of the script.

*Checked:* generated into a test tree, the script is byte for byte the text it
is meant to be, it builds the tar, puts it in `SOURCES`, writes the spec, and
calls `rpmbuild -ba` (with a `rpmbuild` stand-in, since this machine has no
RPM: no RPM package was built). The spec is the same text as the one
`perl scriba-rpm.pl` writes: same preamble, same body, same file list, apart
from the CR the Perl leaves in `Version: 2.0
b0
` and in the `Source:` line,
and apart from the order of the file list, which is sorted here. The list of
the Perl is the entries of `$CONFIGDIR`, `$INCLUDE`, `$SOURCE` and `$MODULE`
and the four files it names one by one; this one is the same entries, sorted
and without a duplicate, and it warns about a file that is not there yet
instead of quietly leaving it out of the package.

**`mkdeb.sh`** (generated by `./setup --install`) needed two things that had
left the tree:

* `deb/DEBIAN/`, the control file and the installation scripts of the package.
  They are in the 1.0b30 release archive (`OLD/upstream-finds/`) and they are
  back in the tree: `control`, `postinst` (which was `#!/bin/sh` already) and
  `preinst`, `prerm`, `postrm`, which were one line Perl scripts that only did
  `exit(0)` and are `#!/bin/sh` scripts here, because the package should not
  ask for a Perl on the machine it is installed on. The `Version:` line of the
  control file is `2.0.0-1`: the same three numbers as `version.txt` plus the
  build number, which is the field the script rewrites.
* `perl preparedeb.pl`, which put the build number into that control file. A
  `sed` does it now, and it changes the *copy* of the control file under
  `deb/scriba/DEBIAN/` instead of the one in the tree the way the Perl script
  did, so building the package does not change a tracked file any more.

The `mv` at the end built the name of the .deb with `cat version.txt` and
`cat build.txt`, which carry CRLF, so the package ended up named
`scriba-v2.0
b0
-1_i386.deb` on UNIX; the CR goes off with the LF here. The
`chown root:root` calls of the script only run as root, with a line saying so
otherwise - `dpkg-deb` builds the package as a normal user and only warns about
the owner, `fakeroot` gives the root owner.

*Checked:* `./mkdeb.sh` in a tree compiled and configured by `./setup`
(`--install` with the directories under a test directory) writes
`deb/scriba-2.0b0-1_i386.deb`, and `dpkg-deb --info` of it shows `Version:
2.0.0-1`, `Architecture: i386` and the four `#!/bin/sh` installation scripts.
The tracked `deb/DEBIAN/control` is not touched by it.

One quirk that was in the Perl and is in the C port too: `./setup --install`
writes `install.sh`, `mkdeb.sh` and `mkrpm.sh` with mode 0500, and a second
`./setup --install` in the same tree dies on "Can not write mkdeb.sh" - remove
the generated scripts first.

## The command reference of the User Guide, and the export definition file

Two more Perl programs of the documentation chain are C now, and both of them
were wired into a build that ran Perl before.

**`mkcdoc.pl` → `tools/codegen/mkcdoc.c`.** The documentation of every command
of the language is in the C sources of the `commands/` directory, in comments
that open with the name of the command and carry the directives `=section`,
`=title`, `=subtitle` and `=display`; the rest of the comment is the
documentation of the command, and `=details`, `=verbatim` and `=itemize` inside
it say how it is to be laid out. `mkcdoc` walks that directory and writes two
things out of it: `commands.texi`, the "Command reference" chapter of the User
Guide, and the HTML pages of the old web site (`commands.html` and one page for
every command that has a `=details` line).

`texi/ug.texi.jam` includes `../commands.texi`, and that file was not in the
tree, so `./mkth.sh all` told the reader that the chapter was missing and the
User Guide came out without its command reference.  `./mkth.sh` writes the file
with `bin/tools/mkcdoc` before it builds `ug` now, and no warning is left: the
chapter is 289 `@section` entries, `bin/texi/ug.texi` is 396 857 bytes with it
against the 258 623 of the guide without a command reference, and the
`mkcdoc.cmd` of the Windows side runs the C program too.

*Checked:* run in a copy of `commands/`, the port writes the same thing as the
Perl program.  `commands.texi` is the chapter of the Perl with the CR taken off
(the Perl writes 139 434 bytes with 1 497 CR in them, the port 138 234 without a
CR), and after that the two differ in 53 lines: three are the places where the
sources use `B<...>` and `I<...>` and the Perl leaves the closing `>` of its own
pattern in the output (`@b{empty}>`, `@b{after}>`, `@emph{root}>`), and fifty are
the places where a `R<...>` reference names another command: `$DISPLAY{$name}` is
looked up with the name of the command, and the keys of that table carry the CR
in the Perl, so the lookup never finds anything on UNIX and the reference is
printed as "COS" while the port prints the display of the command, "COS()", the
way the same program does on Windows.  `commands.html` differs in the same fifty
lines only, and the fifteen detail pages of the two are identical after the CR
of the Perl is taken off their text; their *names* differ, because the Perl names
them "ABS" with a CR before the .html and the port names them "ABS.html".

**`mkscribadef.pl` → `tools/codegen/mkscribadef.c`.** `scriba.def`, the export
definition file of the library for the Windows link of the DLL, is the list of
the functions of `scriba.c`: every comment that holds the word `FUNCTION` is
followed by the declaration of a function, and the name of it is the second word
of that line.  `Makefile.nt.jam` calls `bin\tools\mkscribadef.exe` for it now
instead of the Perl script, and the generated file is in `.gitignore`.

*Checked:* `scriba.def` from the port and from the Perl are the same 69 lines for
the same `scriba.c`, byte for byte.

The documentation chain of the two is otherwise untouched: with the *same*
`commands.texi` the C tools and the Perl tools build `bin/texi/ug.texi` byte for
byte the same 398 057 bytes, so the fifty lines above are the whole difference
that the port makes in the User Guide.  `./mkth.sh all` runs without Perl and, in
a tree where `./setup --install` has run, reports nothing at all.

## The Windows command files, and the `ssplit` tool

The documentation drivers of the Windows side were the last place in the tree
that ran Perl.  `mkth.cmd` is the twin of `mkth.sh` now: it takes the name of a
document of `texi/`, or `all` for every one of them, runs `bin\tools\jamal` with
the directory of the document as the current directory and then `bin\tools\t2h`
on the texi it wrote, so the result is `bin\texi\<name>.texi` and the html pages
next to it, exactly where the makefiles of the modules put `mod_*.texi`.  The
three things `mkth.sh` does besides are in it too: `mkcdoc` writes the command
reference chapter before `ug` is built, `regtest` and `samplebas` are run from
their `tmp/` subdirectory with `bin\exe` on the `PATH`, and a missing
`install.sh`, `mkdeb.sh` or `etc-init.d-sbhttpd` is reported instead of coming
out as an empty listing.

Around it, the command files that used to be separate drivers are calls of it:

| Command file | What it is now |
|--------------|----------------|
| `mkthl.cmd` | the light build: `mkth.cmd` runs neither TeX nor the Microsoft help compiler any more, so the light build and the full one are the same |
| `mkthm.cmd` | `mkth.cmd --texi <page>`: the module makefiles write `bin\texi\mod_<module>.texi`, this converts one to html |
| `mktexihtml.cmd` | `mkth.cmd devguide ug auxfiles source` |
| `mksamples.cmd` | `mkth.cmd <document>` for the two documents of the regression suite |
| `mkath.cmd` | `mkth.cmd all`, and then `mkth.cmd --texi` for every `bin\texi\mod_*.texi` that `make` has left there |

`mkweb.cmd` and `examples/sibawa/mktm.cmd` needed a tool of their own, the
`ssplit.pl` source splitter, which cuts a text file into the files its `%file`
lines name.  It is `tools/codegen/ssplit.c` now and is built with the other
tools, so `mkweb.cmd` splits `html/source.txt` with `bin\tools\ssplit` -- the
first file it names is the macro package `macros.jim` -- and then runs
`bin\tools\jamal` on every `.jam` file with that package, which is the loop
`mkweb.pl` had.  `mktm.cmd` does the same two steps for the template of the
SIBawa example.  `ssplit.pl` and `mkweb.pl` stay in the tree as the reference
originals and are still the source listings of the `auxfiles` document, which
now says where the C tools come in.

The `doc` target of `Makefile.nt.jam` was the last Perl of that file: it read
`modules.sdd` and `scriba.sdd`, two description files that exist neither in the
tree nor in its history, and ran `esd2html.pl` on them.  The target is gone and
`esd2html.pl` is a relic that nothing calls.

*Checked*, for `ssplit` against the Perl on this machine:

| Check | Result |
|-------|--------|
| `perl ssplit.pl html/source.txt out` against `bin/tools/ssplit html/source.txt out` | the same 62 files, byte for byte the same once the CR the Perl leaves on UNIX is taken off.  The port takes the CR off with the LF on either system, the way the other ports of the tree do, see the note at the top of `tools/codegen/ssplit.c` |
| a `%file`/`%dir` fixture, and the second argument as the output directory | the same tree, subdirectory and all |
| a file that did not change between two runs | not rewritten, as in the Perl (the `return if $buffer eq $sbuffer` case) |
| `-h`, and a file that cannot be opened | the same help text, the same error text, and the same exit status `2` |
| text before the first `%file` | the same message, `Is there text before the first '%file' ??`, and status 255 against the errno the Perl happens to exit with |

What could **not** be checked here is the command files themselves.  A `.cmd`
runs on Windows only, and this machine has neither Windows nor Wine, so what is
untested is the batch language of the files and not the build they drive: they
call the same `bin\tools\jamal` and `bin\tools\t2h` that the verified UNIX driver
`mkth.sh` calls, with the same arguments in the same directories.

## Build files that still call Perl

What the build runs: nothing.  What is left in the tree:

* `setup.pl` is the Perl driver of the build. It is no longer the entry point
  (`./setup` runs the C driver, see above) and the build does not need it, but it
  still works and drives the same C tools, so it is kept next to the C port.
* The reference originals of everything that was ported: `jamal.pl`, `t2h.pl`
  and `esd.pm`, then `scriba-tar.pl`, `scriba-zip.pl`, `scriba-rpm.pl`,
  `preparedeb.pl` (the release is made without them), `mkcdoc.pl` and
  `mkscribadef.pl` (see above).
* Dead relics whose input left the tree, none of which any makefile of this tree
  calls any more:
  * `esd2html.pl`, the source documentation to HTML converter: it is a general
    purpose program that reads an `.sdd` description file, and there is no
    `.sdd` file in this tree nor in its history.  The `doc` target of
    `Makefile.nt.jam` was the only place that called it (`modules.sdd`,
    `scriba.sdd`); that target is gone and nothing calls it now.
  * `ssplit.pl` and `mkweb.pl`, which made the web site out of
    `html/source.txt`: the two reference originals of the flow `mkweb.cmd` runs
    with the C tools now (see *The Windows command files and `ssplit`*), and
    `mkweb.pl` still runs `..\jamal.pl` on a Windows path.
  * `convert.pl`, `convert.sh` and `uncr`, the helpers that converted files of a
    release between the CR/LF conventions when they were uploaded.
  * `heber.pl`, a sample external preprocessor of ScriptBasic (HTML embedded
    Basic to BASIC), not a program of the build.
* The six scripts `esd2texi.pl`, `esd2any.pl`, `esdcnv.pl`,
  `moduletablemaker.pl`, `statltmaker.pl` and `lmt_help.pl` were named by this
  section before; they are in `OLD/` with the other archives of the dead Perl,
  not in the tree.
* No `.cmd` command file calls Perl any more.  `mkcdoc.cmd`, `modconfigure.cmd`
  and `setup.cmd` did it already (`bin\tools\mkcdoc`, `bin\tools\jamal.exe`,
  `bin\tools\setup.exe`); `mkth.cmd` is the twin of `mkth.sh` now,
  `mkthl.cmd`, `mkthm.cmd`, `mktexihtml.cmd`, `mksamples.cmd` and `mkath.cmd`
  are calls of it, and `mkweb.cmd` and `examples/sibawa/mktm.cmd` use
  `bin\tools\jamal.exe` and the new `bin\tools\ssplit.exe`.  The command files
  themselves could not be run here -- that needs a Windows machine -- see *The
  Windows command files and `ssplit`*.
* `Makefile.nt.jam` uses no Perl at all now: the code generators and `scriba.def`
  are C programs, and its `doc` target, which called `perl esd2html.pl` with the
  two `.sdd` files that do not exist, is gone.
* `html/source.txt` (historical web source) still names `syntaxer.pl`.
* `setup.log` is a committed build log from 2013, before the C tools existed.
