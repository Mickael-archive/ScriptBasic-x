#! /bin/sh
#
# Create the source distribution of ScriptBasic:
#
#     scriba-<version>b<build>-source.tar.gz
#
# The archive holds exactly the source files of the tree, under a top level
# directory named scriba-<version>b<build>.  This is the role of the old
# scriba-tar.pl: it read the CVS/Entries files and wrote a ./mktar script that
# put exactly the files CVS knew about into the tar, so that the tarball could
# not pick up compiled garbage.  This tree is a git worktree and the tracked
# files play the same role, so the file list comes from git ls-files.
#
# The archive is reproducible: the members are sorted, they carry no owner, no
# group and no modification time of their own, and gzip writes no file name and
# no timestamp either, so packing the same tree twice gives the same bytes.
#
# usage: ./mktar.sh [<output directory>]
#
# Run it from anywhere; the output goes to the current directory, or to the
# directory given as the first argument.  Like scriba-tar.pl it warns about
# files in the list that are not on the disk any more, and it leaves them out.
#
set -e

top=$(cd "$(dirname "$0")" && pwd)

# version.txt and build.txt are CRLF in this tree and the Perl original chomped
# only the LF, so on UNIX it built names like "scriba-2.0\rb0\r" - the CR is
# taken off here.
ver=$(tr -d '\r\n' < "$top/version.txt")
build=$(tr -d '\r\n' < "$top/build.txt")
if [ -z "$ver" ] || [ -z "$build" ] ; then
  echo "mktar.sh: cannot read version.txt / build.txt" >&2
  exit 1
fi

name="scriba-${ver}b${build}"
tarname="$name-source.tar.gz"

out=$(pwd)
if [ $# -gt 0 ] ; then
  out=$(cd "$1" 2>/dev/null && pwd) || { mkdir -p "$1" && out=$(cd "$1" && pwd); }
fi

# The file list.  A released tarball has no .git in it, so this works in a
# checkout only, which is where a release is made.
cd "$top"
if [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" != "true" ] ; then
  echo "mktar.sh: $top is not a git worktree, there is no file list to use" >&2
  echo "mktar.sh: the tracked files of the repository are the source files" >&2
  exit 1
fi

tmplist=$(mktemp) || exit 1
filelist=$(mktemp) || exit 1
trap 'rm -f "$tmplist" "$filelist"' EXIT INT TERM
git ls-files > "$tmplist" || {
  echo "mktar.sh: git ls-files failed" >&2
  exit 1
}

# One name per line: a NUL separated list would be safer, but /bin/sh is dash
# on most systems and its read has no -d.  Refuse the names that would make the
# line format ambiguous instead of packing a wrong archive.
names=$(wc -l < "$tmplist")
nulnames=$(git ls-files -z | tr -cd '\0' | wc -c)
if [ "$names" -ne "$nulnames" ] ; then
  echo "mktar.sh: a tracked file name contains a newline, which this script" >&2
  echo "mktar.sh: cannot put in a one name per line list" >&2
  exit 1
fi
if grep -q '^-' "$tmplist" ; then
  echo "mktar.sh: a tracked file name starts with a hyphen" >&2
  exit 1
fi

# Only the files that exist: a tracked file the worktree deleted is reported,
# not packed - the tar would fail on it.
: > "$filelist"
n=0
missing=0
while IFS= read -r f ; do
  if [ -e "$f" ] ; then
    printf '%s\n' "$f" >> "$filelist"
    n=$((n + 1))
  else
    echo "mktar.sh: $f is tracked but is not on the disk, left out" >&2
    missing=$((missing + 1))
  fi
done < "$tmplist"

if [ $n -eq 0 ] ; then
  echo "mktar.sh: no source file to pack" >&2
  exit 1
fi

# --transform puts the files under $name/, and the fixed owner, group and time
# make the archive byte for byte the same every time.  --verbatim-files-from
# reads the list as it is, so that no name is taken for an option.  gzip -n
# leaves the name and the time out of the gzip header.
tar --format=gnu --numeric-owner --owner=0 --group=0 --mtime=@0 --sort=name \
    --transform "s|^|$name/|" --verbatim-files-from -T "$filelist" -cf - |
  gzip -9n > "$out/$tarname"

echo "$out/$tarname"
echo "$n source files packed"
if [ $missing -ne 0 ] ; then
  echo "$missing tracked files were missing from the disk"
fi
