#!/bin/sh
#
# Copyright 2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
#
# xo-lexicon: build, merge, and check libxo lexicon files
#
# The lexicon directory holds one pair of files per package:
#     ${pkg}.lex               "tag count"
#     ${pkg}.discouraged.lex   "tag [replacement]"
# plus two merged files, which are a cache of the per-package files:
#     system.lex               "tag packages uses"
#     system.discouraged.lex   "tag packages uses [replacement]"
#

LEXDIR="%%XO_LEXICONDIR%%"
BINDIR="%%XO_BINDIR%%"

PROG=`basename "$0"`

usage () {
    cat <<EOF
Usage: $PROG build -p package [-o dir] [-d discouraged.lex] source...
       $PROG merge [-D dir]
       $PROG check [-D dir] [-n] source...
  build   Write \${package}.lex (and \${package}.discouraged.lex) from
          the tags used in the sources; default dir is "."
  merge   Rebuild system.lex and system.discouraged.lex from the
          per-package files in the lexicon directory
  check   Report each tag the sources use as known, discouraged,
          or new; with -n, print only the new tags, one per line
  -D dir  Lexicon directory (default: $LEXDIR)
Environment: XOLINT names the xolint to run (default: xolint)
EOF
    exit 1
}

warn () {
    echo "$PROG: $*" >&2
}

die () {
    warn "$@"
    exit 1
}

find_xolint () {
    if [ -n "$XOLINT" ]; then
        :
    elif [ -x "$BINDIR/xolint" ]; then
        XOLINT="$BINDIR/xolint"
    else
        XOLINT=xolint
    fi
}

# Run "xolint -L" over the sources; output is "tag count", sorted.
# Notes about things xolint cannot see go to stderr.
run_xolint () {
    find_xolint
    case "$XOLINT" in
    *.pl) perl "$XOLINT" -L "$@" ;;
    *) "$XOLINT" -L "$@" ;;
    esac
}

# Strip comments and blank lines
lex_body () {
    sed -e 's/#.*//' -e '/^[ 	]*$/d' "$@"
}

# List the per-package files of one kind ("lex" or "discouraged")
lex_files () {
    for file in "$1"/*.lex; do
        [ -f "$file" ] || continue
        case `basename "$file"` in
        system.lex|system.discouraged.lex) ;;
        *.discouraged.lex) [ "$2" = discouraged ] && echo "$file" ;;
        *) [ "$2" = lex ] && echo "$file" ;;
        esac
    done
}

# Merge the per-package files in $1 into $2 (system.lex) and $3
# (system.discouraged.lex).  Merging is a pure function of the
# directory, so reinstalls and uninstalls need nothing special.
merge_into () {
    dir="$1"
    out_lex="$2"
    out_disc="$3"

    goods=`lex_files "$dir" lex`
    discs=`lex_files "$dir" discouraged`

    {
        echo "# libxo lexicon, merged by $PROG; do not edit"
        echo "# tag packages uses"
        if [ -n "$goods" ]; then
            # One package counts once, however many times it uses
            # the tag; "packages" is the number that matters
            for file in $goods; do
                lex_body "$file" | awk '{ print $1, ($2 == "" ? 1 : $2) }' \
                    | sort -u -k1,1
            done | awk '
                { packages[$1] += 1; uses[$1] += $2 }
                END { for (t in packages) print t, packages[t], uses[t] }
            ' | sort
        fi
    } > "$out_lex"

    {
        echo "# libxo discouraged lexicon, merged by $PROG; do not edit"
        echo "# tag packages uses [replacement]"
        if [ -n "$discs" ]; then
            for file in $discs; do
                lex_body "$file" | awk -v file="$file" \
                    '{ print $1, ($2 == "" ? "-" : $2), file }'
            done | awk -v prog="$PROG" -v lexfile="$out_lex" '
                BEGIN {
                    while ((getline line < lexfile) > 0) {
                        if (line ~ /^#/)
                            continue
                        split(line, f, " ")
                        packages[f[1]] = f[2]
                        uses[f[1]] = f[3]
                    }
                }
                {
                    tag = $1; repl = $2
                    if (!(tag in seen)) {
                        seen[tag] = 1
                        order[++count] = tag
                        rep[tag] = repl
                        from[tag] = $3
                    } else if (repl != "-" && rep[tag] == "-") {
                        rep[tag] = repl
                        from[tag] = $3
                    } else if (repl != "-" && repl != rep[tag]) {
                        printf("%s: %s: %s: replacement \"%s\" conflicts" \
                               " with \"%s\" from %s; keeping \"%s\"\n",
                               prog, $3, tag, repl, rep[tag], from[tag],
                               rep[tag]) > "/dev/stderr"
                    }
                }
                END {
                    for (i = 1; i <= count; i++) {
                        tag = order[i]
                        line = tag " " (tag in packages ? packages[tag] : 0) \
                            " " (tag in uses ? uses[tag] : 0)
                        if (rep[tag] != "-")
                            line = line " " rep[tag]
                        print line
                    }
                }
            ' | sort
        fi
    } > "$out_disc"
}

do_build () {
    pkg=
    outdir=.
    disc=
    OPTIND=1
    while getopts "p:o:d:" opt; do
        case $opt in
        p) pkg="$OPTARG" ;;
        o) outdir="$OPTARG" ;;
        d) disc="$OPTARG" ;;
        *) usage ;;
        esac
    done
    shift `expr $OPTIND - 1`

    [ -n "$pkg" ] || die "build: missing -p package"
    case "$pkg" in
    system|*/*|.*|*.discouraged) die "build: invalid package name: $pkg" ;;
    esac
    [ $# -gt 0 ] || die "build: no source files"
    [ -d "$outdir" ] || die "build: no such directory: $outdir"

    out="$outdir/$pkg.lex"
    tmp="$out.tmp.$$"
    {
        echo "# libxo lexicon for package $pkg, built by $PROG"
        echo "# tag count"
        run_xolint "$@"
    } > "$tmp" || { rm -f "$tmp"; die "build: xolint failed"; }
    mv "$tmp" "$out" || exit 1

    out="$outdir/$pkg.discouraged.lex"
    if [ -n "$disc" ]; then
        [ -f "$disc" ] || die "build: no such file: $disc"
        bad=`lex_body "$disc" | awk 'NF > 2 { print NR ": " $0 }'`
        [ -z "$bad" ] || die "build: $disc: expected \"tag [replacement]\": $bad"
        tmp="$out.tmp.$$"
        cp "$disc" "$tmp" && mv "$tmp" "$out" || exit 1
    else
        rm -f "$out"
    fi
}

do_merge () {
    dir="$LEXDIR"
    OPTIND=1
    while getopts "D:" opt; do
        case $opt in
        D) dir="$OPTARG" ;;
        *) usage ;;
        esac
    done
    shift `expr $OPTIND - 1`
    [ $# -eq 0 ] || dir="$1"
    [ -d "$dir" ] || die "merge: no such directory: $dir"

    tmp_lex=`mktemp "$dir/.system.lex.XXXXXX"` || exit 1
    tmp_disc=`mktemp "$dir/.system.discouraged.lex.XXXXXX"` \
        || { rm -f "$tmp_lex"; exit 1; }
    merge_into "$dir" "$tmp_lex" "$tmp_disc"
    chmod 644 "$tmp_lex" "$tmp_disc"
    mv "$tmp_lex" "$dir/system.lex" && mv "$tmp_disc" "$dir/system.discouraged.lex"
}

do_check () {
    dir="$LEXDIR"
    newonly=
    OPTIND=1
    while getopts "D:n" opt; do
        case $opt in
        D) dir="$OPTARG" ;;
        n) newonly=1 ;;
        *) usage ;;
        esac
    done
    shift `expr $OPTIND - 1`
    [ $# -gt 0 ] || die "check: no source files"

    tmpdir=`mktemp -d "${TMPDIR:-/tmp}/xo-lexicon.XXXXXX"` || exit 1
    trap 'rm -rf "$tmpdir"' 0 1 2 15

    sys_lex="$dir/system.lex"
    sys_disc="$dir/system.discouraged.lex"
    if [ ! -d "$dir" ]; then
        warn "check: no lexicon directory: $dir; every tag is new"
        : > "$tmpdir/system.lex"
        : > "$tmpdir/system.discouraged.lex"
        sys_lex="$tmpdir/system.lex"
        sys_disc="$tmpdir/system.discouraged.lex"
    elif [ ! -f "$sys_lex" -o ! -f "$sys_disc" ] \
            || [ -n "`find "$dir" -name '*.lex' ! -name 'system.*' \
                         -newer "$sys_lex"`" ]; then
        # A missed post-install hook does no harm: merge in memory
        warn "check: $sys_lex is stale; merging in memory"
        sys_lex="$tmpdir/system.lex"
        sys_disc="$tmpdir/system.discouraged.lex"
        merge_into "$dir" "$sys_lex" "$sys_disc"
    fi

    run_xolint "$@" > "$tmpdir/used" || die "check: xolint failed"

    awk -v newonly="$newonly" -v lexfile="$sys_lex" -v discfile="$sys_disc" '
        BEGIN {
            while ((getline line < lexfile) > 0) {
                if (line ~ /^#/)
                    continue
                split(line, f, " ")
                packages[f[1]] = f[2]
            }
            while ((getline line < discfile) > 0) {
                if (line ~ /^#/)
                    continue
                n = split(line, f, " ")
                disc[f[1]] = (n >= 4) ? f[4] : "-"
            }
            if (!newonly)
                printf("%-12s %-32s %5s %5s\n", "# status", "tag",
                       "uses", "pkgs")
        }
        {
            tag = $1; count = $2
            if (newonly) {
                if (!(tag in packages) && !(tag in disc))
                    print tag
            } else if (tag in disc) {
                printf("%-12s %-32s %5d %5d%s\n", "discouraged", tag, count,
                       packages[tag] + 0,
                       disc[tag] == "-" ? "" : "  use " disc[tag])
            } else if (tag in packages) {
                printf("%-12s %-32s %5d %5d\n", "known", tag, count,
                       packages[tag])
            } else {
                printf("%-12s %-32s %5d\n", "new", tag, count)
            }
        }
    ' "$tmpdir/used"
}

[ $# -gt 0 ] || usage
mode="$1"
shift
case "$mode" in
build) do_build "$@" ;;
merge) do_merge "$@" ;;
check) do_check "$@" ;;
*) usage ;;
esac
