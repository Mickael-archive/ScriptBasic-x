ScriptBasic-x
=============

This repo is a fork and a rewrite of the original source repository for the classic ScriptBasic interpreter as or www.scriptbasic.com
The original project is not maintained any more by the original author.

goal is the move the source to C23 and compile with Zig CC.

Layout of the tree:

* `src/` - the C sources of the interpreter and the headers the build generates
  next to them (`src/commands/`, `src/variations/`);
* `build/` - everything that drives a build: the Windows command (`.cmd`) files
  and `build/codegen/`, the C code generators (jamal, t2h, setup, headerer,
  generrh, syntaxer, lmt_make, mkcdoc, mkscribadef, ssplit);
* `texi/`, `html/`, `examples/`, `extensions/`, `test/`, `include/` - the
  documentation sources, the extensions and the samples;
* `OLD/` - the replaced Perl programs and the dead relics, kept for reference.

The UNIX build is `./setup`, the Windows one `build\setup.cmd`; both work from
anywhere. The documents are built by `./mkth.sh` (`build\mkth.cmd`).

Action plan will follow soon. (today 11-of-october-2026)

Mickael CALA
