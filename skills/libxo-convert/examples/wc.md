# Example: wc

A complete small conversion, from FreeBSD's 2014 wc.c.  The 2014
conversion is in libxo's patches/wc.patch; this one follows the skill
and fixes that patch's mistakes (listed at the end).  Only the parts
that change are shown.

## Before

    static uintmax_t tlinect, twordct, tcharct, tlongline;
    static int doline, doword, dochar, domulti, dolongline;
    static volatile sig_atomic_t siginfo;

    int
    main(int argc, char *argv[])
    {
        ...
        (void) setlocale(LC_CTYPE, "");

        while ((ch = getopt(argc, argv, "clmwL")) != -1)
            ...
        if (doline + doword + dochar + domulti + dolongline == 0)
            doline = doword = dochar = 1;

        errors = 0;
        total = 0;
        if (!*argv) {
            if (cnt((char *)NULL) != 0)
                ++errors;
        } else {
            do {
                if (cnt(*argv) != 0)
                    ++errors;
                ++total;
            } while(*++argv);
        }

        if (total > 1)
            show_cnt("total", tlinect, twordct, tcharct, tlongline);
        exit(errors == 0 ? 0 : 1);
    }

    static void
    show_cnt(const char *file, uintmax_t linect, uintmax_t wordct,
        uintmax_t charct, uintmax_t llct)
    {
        FILE *out;

        if (!siginfo)
            out = stdout;
        else {
            out = stderr;
            siginfo = 0;
        }

        if (doline)
            (void)fprintf(out, " %7ju", linect);
        if (doword)
            (void)fprintf(out, " %7ju", wordct);
        if (dochar || domulti)
            (void)fprintf(out, " %7ju", charct);
        if (dolongline)
            (void)fprintf(out, " %7ju", llct);
        if (file != NULL)
            (void)fprintf(out, " %s\n", file);
        else
            (void)fprintf(out, "\n");
    }

cnt() calls show_cnt() for each file, and also from its read loop
when SIGINFO arrives (to stderr).  Errors use warn(); usage() uses
fprintf(stderr) and exit(1).

## Phase 1 notes

- Output: show_cnt() only, to stdout or (SIGINFO) stderr.
- Loops: the file loop in main; one show_cnt per file.
- Errors: warn() in cnt(), five places; fprintf(stderr) in usage().
- Exits: exit(1) in usage(), before any output; the final exit.

## Phase 2: schema

    +-- count-information
       +-- file*                           one per input; no key
       |  +-- filename?        %s          file; absent for stdin
       |  +-- lines?           %ju         linect; -l
       |  +-- words?           %ju         wordct; -w
       |  +-- bytes?           %ju         charct; -c
       |  +-- characters?      %ju         charct; -m
       |  +-- max-line-length? %ju         llct; -L
       +-- total?                          only with 2 or more files
          +-- lines?           %ju
          +-- words?           %ju
          +-- bytes?           %ju
          +-- characters?      %ju
          +-- max-line-length? %ju

Notes for the review:

- "file" has no key: "wc a a" counts the same file twice, so
  filename is not unique, and stdin has no name.
- -c counts bytes and -m counts characters.  The text prints both
  with the same code, but they are different quantities, so they get
  different names.  (That is not a name changing with a flag: the
  flag selects a different value.)
- -L reports the length of the longest line, not a count of lines;
  "max-line-length", not "long-lines".
- "total" in the text is a label, not a file name; the totals are a
  sibling container of the list, not an instance in it.
- The SIGINFO output goes to stderr in text only.  It is a progress
  report, not part of the data.
- Top container: "count-information", what wc reports.

The review package would also hold count.yang (references/yang.md)
and count.new-words.

## After

    #include <libxo/xo.h>

    static uintmax_t tlinect, twordct, tcharct, tlongline;
    static int doline, doword, dochar, domulti, dolongline;
    static volatile sig_atomic_t siginfo;
    static xo_handle_t *stderr_handle;

    static void	show_cnt(const char *file, int is_total,
                    uintmax_t linect, uintmax_t wordct,
                    uintmax_t charct, uintmax_t llct);

    int
    main(int argc, char *argv[])
    {
        ...
        (void) setlocale(LC_CTYPE, "");

        argc = xo_parse_args(argc, argv);
        if (argc < 0)
            exit(EXIT_FAILURE);

        while ((ch = getopt(argc, argv, "clmwL")) != -1)
            ...
        if (doline + doword + dochar + domulti + dolongline == 0)
            doline = doword = dochar = 1;

        stderr_handle = xo_create_to_file(stderr, XO_STYLE_TEXT, 0);

        xo_open_container("count-information");
        xo_open_list("file");

        errors = 0;
        total = 0;
        if (!*argv) {
            xo_open_instance("file");
            if (cnt((char *)NULL) != 0)
                ++errors;
            xo_close_instance("file");
        } else {
            do {
                xo_open_instance("file");
                if (cnt(*argv) != 0)
                    ++errors;
                xo_close_instance("file");
                ++total;
            } while(*++argv);
        }

        xo_close_list("file");

        if (total > 1) {
            xo_open_container("total");
            show_cnt("total", 1, tlinect, twordct, tcharct, tlongline);
            xo_close_container("total");
        }

        xo_close_container("count-information");
        xo_finish();
        exit(errors == 0 ? 0 : 1);
    }

    static void
    show_cnt(const char *file, int is_total, uintmax_t linect,
        uintmax_t wordct, uintmax_t charct, uintmax_t llct)
    {
        xo_handle_t *xop;

        if (!siginfo)
            xop = NULL;
        else {
            xop = stderr_handle;
            siginfo = 0;
        }

        if (doline)
            xo_emit_h(xop, " {:lines/%7ju/%ju}", linect);
        if (doword)
            xo_emit_h(xop, " {:words/%7ju/%ju}", wordct);
        if (dochar)
            xo_emit_h(xop, " {:bytes/%7ju/%ju}", charct);
        if (domulti)
            xo_emit_h(xop, " {:characters/%7ju/%ju}", charct);
        if (dolongline)
            xo_emit_h(xop, " {:max-line-length/%7ju/%ju}", llct);
        if (is_total)
            xo_emit_h(xop, " {L:total}\n");
        else if (file != NULL)
            xo_emit_h(xop, " {:filename/%s}\n", file);
        else
            xo_emit_h(xop, "\n");

        if (xop != NULL)
            xo_flush_h(xop);
    }

The other show_cnt() calls in cnt() gain the "0" is_total argument.
warn() becomes xo_warn() (five places); usage() becomes:

    xo_error("usage: wc [-Lclmw] [file ...]\n");
    exit(1);

The Makefile gains "LIBADD+= xo"; the man page gains --libxo.

## Check the text did not change

- "dochar || domulti" became two separate tests.  That keeps the text
  only if at most one of them is ever set: in FreeBSD's wc.c, "case
  'c'" sets dochar and clears domulti, and "case 'm'" does the
  reverse.  Check that in the getopt loop of the source being
  converted, and cover it in the cases file ("wc -c f", "wc -m f",
  "wc -cm f", "wc -mc f").  If both can be set, keep one test and one
  name.
- The final " total\n" is the same bytes, now from {L:}.
- SIGINFO output still goes to stderr, and appears at once because of
  xo_flush_h.

## Result (JSON)

    $ wc --libxo:JP a /dev/null
    {
      "count-information": {
        "file": [
          { "lines": 1, "words": 2, "bytes": 3, "filename": "a" },
          { "lines": 0, "words": 0, "bytes": 0, "filename": "/dev/null" }
        ],
        "total": { "lines": 1, "words": 2, "bytes": 3 }
      }
    }

(Reformatted; libxo puts one member per line.)

## What changed from the 2014 patch

- "total" was a container opened inside the "file" list, between
  instances; it is now a sibling of the list.
- The totals were encoded with filename "total"; the word is now a
  label.
- "{ek:filename}" was emitted to the default handle at the top of
  show_cnt, so the SIGINFO path wrote a stray filename into stdout's
  encoded output, and the key was not unique anyway.  The filename is
  now emitted once, through the same handle as the counts, and is not
  a key.
- "long-lines" became "max-line-length"; -c and -m are no longer both
  "characters".
- The stderr handle is flushed; before, SIGINFO output was lost.
- "return argc" after a failed xo_parse_args() exited with status 255;
  it is now exit(EXIT_FAILURE).
- The Makefile used DPADD/LDADD; it now uses LIBADD.
- The top container "wc" became "count-information".
