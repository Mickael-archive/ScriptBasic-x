# OLD

Archived files that are no longer part of the build or the maintained source
tree. They are kept here only for historical reference and are **not compiled,
generated from, or shipped**.

## Perl code generators (replaced by C tools in `tools/codegen/`)

The following Perl scripts used to generate C sources at build time. They have
been ported to portable C23 programs that produce byte-identical output and are
now built and invoked by `setup.pl` / `makefile.jam`:

| Archived Perl script | C replacement               | Generates                              |
|----------------------|-----------------------------|----------------------------------------|
| `generrh.pl`         | `tools/codegen/generrh.c`   | `errcodes.c`, `include/error.bas`      |
| `headerer.pl`        | `tools/codegen/headerer.c`  | the `*.h` headers from the `*.c` files |
| `syntaxer.pl`        | `tools/codegen/syntaxer.c`  | `syntax.c`, `notimp.c`                 |
| `lmt_make.pl`        | `tools/codegen/lmt_make.c`  | `lmt_*.c` from `lmt_*.def`             |

The C replacements are functionally equivalent. Note that `syntaxer.pl` was not
deterministic (it iterated a Perl hash), so `syntaxer.c` additionally fixes the
output order to be reproducible.

## Known remaining mentions (for later cleanup steps)

The Perl scripts are gone from the build, but some legacy names still appear:

* The generated `errcodes.c`, `syntax.c`, `notimp.c`, `lmt_*.c` and
  `include/error.bas` still carry a "...created by xxx.pl" banner because the
  generators emit that text. The in-source comments and the `*.def` files have
  already been updated to point at the C tools.
* Legacy build files still contain `perl xxx.pl` recipes: `Makefile.nt.jam`
  (Windows), `Makefile.old`, `Makefile.solaris` and
  `extensions/psql/Makefile.sample.pgsql`.
* `html/source.txt` (historical web source) still names `syntaxer.pl`.
* `headerer.bas`, a ScriptBasic re-implementation of `headerer.pl`, is superseded
  by the same C tool and is a candidate for this directory as well.
