# The 13 references the tree lost: where the files really are

`texi/mod_*.texi.jam` (17 files) name a module source in their `FILE=` macro, and
`html/texi/skeleton.jim` is what the 16 `extensions/*/manual.texi.jam` asked for
before that include was repointed at `texi/skeleton.jim`.  This directory holds
the copies recovered from the released sources, so the analysis can be repeated
without another download.  Nothing here is part of the build.

## Where they were searched for

| Place | Result |
|-------|--------|
| `github.com/verhas/ScriptBasic` (6 commits, no tags) | the 12 `.c` names appear in **no** commit, and `html/texi/` is absent |
| its forks (`ScriptBasic/ScriptBasic`, `rexxitall`, `bryanchance`, `tomirose88-web`, `mickael-cala`) | same tree, `extensions/md5` and `extensions/tools` are 404 |
| `github.com/emtee40/scriptbasic-dev` (community tree, 134 commits, the "sb-dev" repository the upstream README points at) and `emtee40/scriptbasic`, `emtee40/scriptbasic-extensions` | the names appear in **no** commit; but this tree **does** carry `html/texi/skeleton.jim` |
| `gitlab.com` (project search + code search) | no ScriptBasic mirror at all |
| SourceForge project `scriptbasic`, `files/OldFiles/` | **found**: the 1.0b22 … 1.0b30 source archives |

## The archives (in `archive/`, sha256)

| File | Released | sha256 |
|------|----------|--------|
| `scriba-v10b22.tar.gz` | 2001-03-23 | e153ec92f0e519833d3564b175ceed37ac0f9f46c305f0579c4e02ca26abc1f8 |
| `scriba-v10b24-source.tar.gz` | 2001-06-11 | 58bd7323e4fc19a8df8a729bc934bb807bbb759d82332de08886d1bf030ca821 |
| `scriba-v1.0b26-source.tar.gz` | 2001-10-11 | 3ae1eb4a3ff4781433058f7ae491dcdc24e2a43f956ea628769720a932df29d0 |
| `scriba-v1.0b28-source.tar.gz` | 2002-01-16 | 368ae94993f3f19bdc5f16c0fead9583c187eaf413714c646182453f9d10c42c |
| `scriba-v1.0b30-source.tar.gz` | 2003-06-16 | 30eb00bc5bdd495f7ed8f2369e64116bec6372b2d4a9c967b25ae7823833de14 |

Downloaded with
`https://sourceforge.net/projects/scriptbasic/files/OldFiles/<name>/download`.

## What each reference turns out to be

| `FILE=` in `texi/mod_*.texi.jam` | found | copy here | relation to the current tree |
|---|---|---|---|
| `cginterf.c` | 1.0b24, b26, b28 | `b28/cginterf.c` (44 085 b) | old name of `extensions/cgi/interface.c`; its `/** … */` block is the same text, shorter (383 vs 433 lines) |
| `cio.c` | b24, b26, b28 | `b28/cio.c` | old name of `extensions/cio/interface.c`; doc block identical |
| `curlintrfc.c` | b28 only | `b28/curlintrfc.c` (2 490 b) | the curl module was new in b28 and nearly undocumented (10 doc lines vs 1 234 today) |
| `curses.c` | **nowhere** | — | the curses module first appears in b30, already as `interface.c`; the `FILE=` is a copy of the old template, the name never existed |
| `md5interf.c` | b22, b24, b26 | `b26/md5interf.c` + `md5.c`, `md5.h`, `global.h` | module removed between b26 and b28; documented one function, `MD5("string")`, as `besFUNCTION(md5fun)` |
| `mt.c` | b24, b26, b28 | `b28/mt.c` | old name of `extensions/mt/interface.c`; same start of the doc, 231 vs 309 lines today |
| `nt.c` | b24, b26, b28 | `b28/nt.c` | old name of `extensions/nt/interface.c`; 323 vs 326 doc lines |
| `odbcinterf.c` | b28 only | `b28/odbcinterf.c` | old name of `extensions/odbc/interface.c`; doc block identical |
| `reinterf.c` | b22, b24, b26, b28 | `b28/reinterf.c` | old name of `extensions/re/interface.c`; 111 vs 123 doc lines |
| `tools.c` | b26, b28 | `b28/tools.c` | module removed before b30; documents `t::ArrayToString`, `StringToArray`, `SaveString`, `LoadString`, `MD5`, `ArrayToStringMD5` -- **the `t` module is its successor**, same `=section` names |
| `ux.c` | b24, b26, b28 | `b28/ux.c` | old name of `extensions/ux/interface.c`; doc block **byte identical** |
| `zlbintrf.c` | b22, b24, b26 | `b26/zlbintrf.c` | old name of `extensions/zlib/interface.c`; doc block **byte identical** |
| `html/texi/skeleton.jim` | b30 and `emtee40/scriptbasic-dev` | `b30/skeleton.jim`, `dev-repo/skeleton.jim` | **the same file as `texi/skeleton.jim`**: the b30 copy is identical apart from the line endings (LF), the dev-repo copy is byte identical |

The rename to `extensions/<module>/interface.c` happened between 1.0b28 (2002-01)
and 1.0b30 (2003-06); the `texi/mod_*.texi.jam` `FILE=` macros were never updated,
and the pages were never built from them again -- `mkath.cmd` builds the module
pages with `mkthm`, that is from `bin/texi/mod_<module>.texi` produced by the
module makefile out of `extensions/<module>/manual.texi.jam`.
