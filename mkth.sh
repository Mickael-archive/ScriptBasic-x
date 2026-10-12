#! /bin/sh
#
# Build the ScriptBasic documents: the *.texi.jam files of texi/, esd.texi.jam
# and the module pages the module makefiles write.  It is the UNIX half of the
# documentation driver (mkth.cmd is the Windows one, mkath.cmd calls it for
# every document).  This version uses the C tools of tools/codegen: no Perl and
# no LaTeX.  Each document produces <name>.texi and the html pages of it in
# bin/texi, next to the module pages.
#
# usage: ./mkth.sh <name> [<name> ...]
#        ./mkth.sh all
#
# Run ./setup first, so that bin/tools/jamal and bin/tools/t2h exist.
#
# Two include mechanisms meet here and they resolve file names differently:
#
#   * `#esd::include <file>' (esd.pm) opens <file> relative to the *current
#     directory*, and
#   * jamal's own `#include <file>' relative to the *directory of the file that
#     includes it*.
#
# The documents are written for a current directory two levels below the source
# root in the old html/texi harness.  In this tree texi/ is one level below the
# root, so the documents name their files accordingly (../FILE_NAME) and jamal
# runs with the current directory set to the document's own directory: for the
# guides that is texi/, for esd.texi.jam the root.  Then both mechanisms look in
# the source root, which is where the files are.  Run the script from anywhere.
#
set -e

top=`cd \`dirname "$0"\` && pwd`
jamal="$top/bin/tools/jamal"
t2h="$top/bin/tools/t2h"
out="$top/bin/texi"

# every document the driver knows about
allnames="esd auxfiles devguide docudoc eszterengine prep_dbg source ug"

if [ $# -eq 0 ] ; then
  echo "usage: $0 <name> [<name> ...]" >&2
  echo "       $0 all" >&2
  echo "the documents are the *.texi.jam files of texi/ and esd" >&2
  exit 1
  fi

if [ "$1" = all ] ; then
  set -- $allnames
  fi

if [ ! -x "$jamal" ] ; then
  echo "$0: $jamal does not exist, run ./setup first" >&2
  exit 1
  fi
if [ ! -x "$t2h" ] ; then
  echo "$0: $t2h does not exist, run ./setup first" >&2
  exit 1
  fi

mkdir -p "$out"

ERRCOUNT=0

for name in "$@" ; do
  echo "processing $name"
  case $name in
    esd) src="$top/esd.texi.jam"; where="$top";;
    *)   src="$top/texi/$name.texi.jam"; where="$top/texi";;
  esac
  if [ ! -f "$src" ] ; then
    echo "###ERROR $name: no such document ($src)"
    ERRCOUNT=`expr $ERRCOUNT + 1`
    continue
    fi

  # The command reference chapter of the User Guide is commands.texi, made out
  # of the documentation blocks of the commands/ sources by mkcdoc (the C port
  # of mkcdoc.pl).  It is written into the top directory, where the
  # <%#include ../commands.texi%> of texi/ug.texi.jam looks for it.
  if [ "$name" = ug ] ; then
    if [ -x "$top/bin/tools/mkcdoc" ] ; then
      ( cd "$top" && ./bin/tools/mkcdoc --texi commands.texi ) || \
        echo "###WARNING ug: the command reference could not be created"
    else
      echo "###WARNING ug: bin/tools/mkcdoc is missing, run ./setup first."
      fi
    fi

  # The documents that cannot be complete in this source tree, and why.  They
  # are built anyway, the missing part is reported as it was before.
  case $name in
    regtest|samplebas)
      echo "###WARNING $name: it calls the jamal extension 'samples', which is a"
      echo "###WARNING $name: Perl module that is not in this tree, and its every"
      echo "###WARNING $name: example and output section comes from it.  It also"
      echo "###WARNING $name: runs scriba, and samplebas.texi.jam has a Windows"
      echo "###WARNING $name: path compiled into SCRIBAEXE."
      ;;
    ug)
      if [ ! -f "$top/commands.texi" ] ; then
        echo "###WARNING ug: commands.texi is missing, so the command reference"
        echo "###WARNING ug: chapter comes out empty.  It is generated from the"
        echo "###WARNING ug: commands/ sources by bin/tools/mkcdoc."
        fi;;
    auxfiles)
      for f in install.sh mkdeb.sh ; do
        if [ ! -f "$top/$f" ] ; then
          echo "###WARNING auxfiles: $f is missing, so its listing comes out empty."
          echo "###WARNING auxfiles: ./setup --install creates it."
          fi
        done;;
    esac

  ( cd "$where" && "$jamal" "$name.texi.jam" "$out/$name.texi" )
  # t2h takes the directory of its output from its second argument, so the html
  # lands next to the texi file.  Given a bare name the Perl original writes it
  # into the current directory instead, and then the build litters the tree.
  "$t2h" "$out/$name.texi" "$out/$name"
  done

if [ $ERRCOUNT -ne 0 ] ; then
  echo "there were $ERRCOUNT errors"
  exit 1
  fi
exit 0
