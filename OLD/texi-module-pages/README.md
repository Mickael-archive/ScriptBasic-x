# The old per-module texi pages

These 17 `mod_*.texi.jam` files used to be the source of the module manual pages,
run by `mkth.cmd` / `mkth.sh` from `html/texi`.  They are archived here because
nothing builds them any more and most of them cannot be built at all:

* `make` documents a module from `extensions/<module>/manual.texi.jam`, through
  the module makefile, into `bin/texi/mod_<module>.texi`;
* `mkath.cmd` converts that built page with `mkthm`, and never calls `mkth` for a
  module.

Nothing is lost by archiving them.  Every page they describe is produced today
from `extensions/<module>/manual.texi.jam`, and for the modules that still exist
the two files produce the same page (compared byte for byte: `cgi`, `cio`, `mt`,
`nt`, `odbc`, `re`, `ux`, `zlib` identical; `curl` differs by a blank line,
`curses` by its `@author` line, which the old file pins to Berki Lukacs Tamas);
`bdb` and `mysql` are byte identical to their modern counterpart, `gd` differs by
one wording fix that only the modern file has.

| File | `FILE=` it names | What it was |
|------|------------------|-------------|
| `mod_bdb.texi.jam` | -- (text inline) | duplicate of `extensions/bdb/manual.texi.jam` |
| `mod_cgi.texi.jam` | `cginterf.c` | old name of `extensions/cgi/interface.c` |
| `mod_ci.texi.jam`, `mod_cio.texi.jam` | `cio.c` | two copies of the same page for `extensions/cio/interface.c` |
| `mod_curl.texi.jam` | `curlintrfc.c` | old name of `extensions/curl/interface.c` |
| `mod_curses.texi.jam` | `curses.c` | a name that never existed: the curses module first appears in 1.0b30, already as `interface.c` |
| `mod_gd.texi.jam` | -- (text inline) | older wording of `extensions/gd/manual.texi.jam` |
| `mod_hash.texi.jam` | -- (text inline) | **the live text**: it was moved to `extensions/hash/manual.texi.jam`, which the build reads (see below) |
| `mod_md5.texi.jam` | `md5interf.c` | module removed between 1.0b26 and 1.0b28; its `MD5("string")` is `t::MD5` today |
| `mod_mt.texi.jam` | `mt.c` | old name of `extensions/mt/interface.c` |
| `mod_mysql.texi.jam` | -- (text inline) | duplicate of `extensions/mysql/manual.texi.jam` |
| `mod_nt.texi.jam` | `nt.c` | old name of `extensions/nt/interface.c` |
| `mod_odbc.texi.jam` | `odbcinterf.c` | old name of `extensions/odbc/interface.c` |
| `mod_re.texi.jam` | `reinterf.c` | old name of `extensions/re/interface.c` |
| `mod_tools.texi.jam` | `tools.c` | module removed before 1.0b30; it is the ancestor of the `t` module, same `=section` names |
| `mod_ux.texi.jam` | `ux.c` | old name of `extensions/ux/interface.c` |
| `mod_zlib.texi.jam` | `zlbintrf.c` | old name of `extensions/zlib/interface.c` |

The `FILE=` names date from before the 1.0b30 rename to
`extensions/<module>/interface.c` and were never updated afterwards; the copies
of every one of those sources, recovered from the 1.0b22 - 1.0b30 release
archives, are in `../upstream-finds/` together with the archaeology.

Two more reasons no page could come out of them: `{#esd::include}` resolves its
argument against the *working directory*, so `../../extensions/...` inside
`skeleton.jim` only lands on the tree when the document is processed from a
directory two levels below the source root (`extensions/<module>` or
`html/texi`); and the two `md5`/`tools` pages have no text of their own, they are
five line skeleton includes waiting for a source file that no longer exists.

## What happened to `hash`

`mod_hash.texi.jam` was the only one of these files whose page was **not**
produced by the tree any more: `extensions/hash/interface.c` has no `/** ... */`
documentation block, so the modern page was empty (455 bytes, empty
"Introduction").  `bdb`, `gd` and `mysql` had been migrated earlier; `hash` was
missed.  Its text now lives in `extensions/hash/manual.texi.jam`, exactly like
those three, and `make` builds the full 17 578 byte page again.
