/*
 * Copyright (c) 2014-2023, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 *
 * This is the implementation of libxo, the formatting library that
 * generates multiple styles of output from a single code path.
 * Command line utilities can have their normal text output while
 * automation tools can see XML or JSON output, and web tools can use
 * HTML output that encodes the text output annotated with additional
 * information.  Specialized encoders can be built that allow custom
 * encoding including binary ones like CBOR, thrift, protobufs, etc.
 *
 * Full documentation is available in ./doc/libxo.txt or online at:
 *   http://juniper.github.io/libxo/libxo-manual.html
 *
 * For first time readers, the core bits of code to start looking at are:
 * - xo_do_emit() -- parse and emit a set of fields
 * - xo_do_emit_fields -- the central function of the library
 * - xo_do_format_field() -- handles formatting a single field
 * - xo_transiton() -- the state machine that keeps things sane
 * and of course the "xo_handle_t" data structure, which carries all
 * configuration and state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <wchar.h>
#include <locale.h>
#include <sys/types.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <wctype.h>
#include <getopt.h>

#include "xo_config.h"

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif /* HAVE_LANGINFO_H */

#ifdef HAVE_EXTERR
#include <exterr.h>
#ifdef HAVE_SYS_EXTERRVAR_H
#include <sys/exterrvar.h>
#else /* HAVE_SYS_EXTERRVAR_H */
#define UEXTERROR_MAXLEN 256    /* A reasonable guess */
#endif /* HAVE_SYS_EXTERRVAR_H */
#else /* HAVE_EXTERR */
#define UEXTERROR_MAXLEN 1	/* Fake size for exterr buffer */
#endif /* HAVE_EXTERR */

#ifdef LIBXO_TEXT_ONLY		/* Turn off unneeded features */
#undef LIBXO_NEED_MAP		/* No tag maps in text mode */
#undef LIBXO_NEED_FILTERS	/* No filters in text mode */
#else  /* LIBXO_TEXT_ONLY */
/* We don't want the overhead of tag maps when in text-only mode */
#define LIBXO_NEED_MAP		/* Map tags to new names */
#endif /* LIBXO_TEXT_ONLY */

#define XO_WANT_FILTER_FLAG
#include "xo.h"
#include "xo_private.h"
#include "xo_encoder.h"
#include "xo_buf.h"
#include "xo_explicit.h"
#include "xo_dyld.h"
#include "xo_format.h"
#include "../filter/xo_filter.h"

/*
 * We ask wcwidth() to do an impossible job, really.  It's supposed to
 * need to tell us the number of columns consumed to display a unicode
 * character.  It returns that number without any sort of context, but
 * we know they are characters whose glyph differs based on placement
 * (end of word, middle of word, etc) and many that affect characters
 * previously emitted.  Without content, it can't hope to tell us.
 * But it's the only standard tool we've got, so we use it.  We would
 * use wcswidth() but it typically just loops through adding the results
 * of wcwidth() calls in an entirely unhelpful way.
 *
 * Even then, there are many poor implementations (macosx), so we have
 * to carry our own.  We could have configure.ac test this (with
 * something like 'assert(wcwidth(0x200d) == 0)'), but it would have
 * to run a binary, which breaks cross-compilation.  Hmm... I could
 * run this test at init time and make a warning for our dear user.
 *
 * Anyhow, it remains a best-effort sort of thing.  And it's all made
 * more hopeless because we assume the display code doing the rendering is
 * playing by the same rules we are.  If it display 0x200d as a square
 * box or a funky question mark, the output will be hosed.
 */
#ifdef LIBXO_WCWIDTH
#include "xo_wcwidth.h"
#else /* LIBXO_WCWIDTH */
#define xo_wcwidth(_x) wcwidth(_x)
#endif /* LIBXO_WCWIDTH */
#include "xo_utf8.h"

#ifdef HAVE_STDIO_EXT_H
#include <stdio_ext.h>
#endif /* HAVE_STDIO_EXT_H */

/*
 * humanize_number is a great function, unless you don't have it.  So
 * we carry one in our pocket.
 */
#ifdef HAVE_HUMANIZE_NUMBER
#include <libutil.h>
#define xo_humanize_number humanize_number 
#else /* HAVE_HUMANIZE_NUMBER */
#include "xo_humanize.h"
#endif /* HAVE_HUMANIZE_NUMBER */

#ifdef HAVE_GETTEXT
#include <libintl.h>
#endif /* HAVE_GETTEXT */

#if HAVE_ETEXT == 1		/* Symbol */
extern char etext;
#endif /* HAVE_ETEXT */

/* Rather lame that we can't count on these... */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/*
 * XO_MAX_FIELDS: the largest number of fields in a format string.
 * With no max, there's no maximum stack impact, so we choose a
 * criminally large default which the build environment can override.
 */
#ifndef XO_MAX_FIELDS
#define XO_MAX_FIELDS (8*1024)	/* Suitably large limit */
#endif /* XO_MAX_FIELDS */

/* Make our own version for older versions of GCC that don't have this */
#ifndef __GNUC_PREREQ
#define __GNUC_PREREQ(maj,min) \
    ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif /* __GNUC_PREREQ */

/*
 * Three styles of specifying thread-local variables are supported.
 * configure.ac has the brains to run each possibility through the
 * compiler and see what works; we are left to define the THREAD_LOCAL
 * macro to the right value.  Most toolchains (clang, gcc) use
 * "before", but some (borland) use "after" and I've heard of some
 * (ms) that use __declspec.  Any others out there?
 */
#define THREAD_LOCAL_before 1
#define THREAD_LOCAL_after 2
#define THREAD_LOCAL_declspec 3

#ifndef HAVE_THREAD_LOCAL
#define THREAD_LOCAL(_x) _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_before
#define THREAD_LOCAL(_x) __thread _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_after
#define THREAD_LOCAL(_x) _x __thread
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_declspec
#define THREAD_LOCAL(_x) __declspec(_x)
#else
#error unknown thread-local setting
#endif /* HAVE_THREADS_H */

#define XO_FAKE_TOP_LEVEL_NAME "data"

const char xo_version[] = LIBXO_VERSION;
const char xo_version_extra[] = LIBXO_VERSION_EXTRA;

#define UNUSED XO_UNUSED

#ifndef LIBXO_TEXT_ONLY
#define XO_MAP_INCR 128		/* Must be even */

#ifndef XO_MAPDIR
#define XO_MAPDIR XO_SHAREDIR "/map"
#endif /* XO_MAPDIR */
#endif /* LIBXO_TEXT_ONLY */

#define XO_INDENT_BY 2	/* Amount to indent when pretty printing */
#define XO_DEPTH	128	 /* Default stack depth */
#define XO_MAX_ANCHOR_WIDTH (8*1024) /* Anything wider is just silly */

#define XO_FAILURE_NAME	"failure"

/* Flags for the stack frame */
typedef unsigned xo_xsf_flags_t; /* XSF_* flags */
#define XSF_NOT_FIRST	(1<<0)	/* Not the first element */
#define XSF_LIST	(1<<1)	/* Frame is a list */
#define XSF_INSTANCE	(1<<2)	/* Frame is an instance */
#define XSF_DTRT	(1<<3)	/* Save the name for DTRT mode */

#define XSF_CONTENT	(1<<4)	/* Some content has been emitted */
#define XSF_EMIT	(1<<5)	/* Some field has been emitted */
#define XSF_EMIT_KEY	(1<<6)	/* A key has been emitted */
#define XSF_EMIT_LEAF_LIST (1<<7) /* A leaf-list field has been emitted */

#define XSF_FILTER	(1<<8)	/* Process any filtering */

/* These are the flags we propagate between markers and their parents */
#define XSF_MARKER_FLAGS \
 (XSF_NOT_FIRST | XSF_CONTENT | XSF_EMIT | XSF_EMIT_KEY | XSF_EMIT_LEAF_LIST )

/*
 * Bits of xs_flags on a parent frame that a JSON/XML open modifies and
 * that must be restored if the child element is later discarded by the
 * filter's rollback.
 */
#define XSF_RB_BITS (XSF_NOT_FIRST | XSF_CONTENT)

/*
 * Turn the transition between two states into a number suitable for
 * a "switch" statement.
 */
#define XSS_TRANSITION(_old, _new) ((_old) << 8 | (_new))

/* Options used by name, but not "real": not saved in xo handle */
#define XO_OPT_NO_COLOR		1 /* Ignore colors */
#define XO_OPT_INDENT		2 /* Indent by given number */
#define XO_OPT_ENCODER		3 /* Use a specific encoder */
#define XO_OPT_MAP		4 /* Map field names */
#define XO_OPT_MAP_FILE		5 /* Use file full of field names mappings */
#define XO_OPT_FILTER		6 /* Filter output using path */
#define XO_EXTERR_BRIEF		7 /* Display brief extended error info */
#define XO_EXTERR_VERBOSE	8 /* Display verbose exterr info */

/*
 * xo_stack_t: As we open and close containers and levels, we
 * create a stack of frames to track them.  This is needed for
 * XOF_WARN and XOF_XPATH.
 */
typedef struct xo_stack_s {
    xo_xsf_flags_t xs_flags;	/* Flags for this frame */
    xo_state_t xs_state;	/* State for this stack frame */
    xo_filter_status_t xs_fstatus; /* Filter status */
    xo_off_t xs_rb_off;		/* Offset of buffer before this level */
    xo_off_t xs_tag_end;	/* Offset just after this level's opening tag */
    xo_off_t xs_key_off;	/* Offset of end of last key renderer */
    xo_xsf_flags_t xs_rb_flags; /* Parent XSF_RB_BITS  at rb-marker time */
    char *xs_name;		/* Name (for XPath value) */
    char *xs_keys;		/* XPath predicate for any key fields */
} xo_stack_t;

#define XS_OFFSET_CLEAR -1	/* Used to make a "not in use" offset */

/*
 * libxo supports colors and effects, for those who like them.
 * XO_COL_* ("colors") refers to fancy ansi codes, while X__EFF_*
 * ("effects") are bits since we need to maintain state.
 */
typedef uint8_t xo_color_t;
#define XO_COL_DEFAULT		0
#define XO_COL_BLACK		1
#define XO_COL_RED		2
#define XO_COL_GREEN		3
#define XO_COL_YELLOW		4
#define XO_COL_BLUE		5
#define XO_COL_MAGENTA		6
#define XO_COL_CYAN		7
#define XO_COL_WHITE		8

#define XO_NUM_COLORS		9

/*
 * Yes, there's no blink.  We're civilized.  We like users.  Blink
 * isn't something one does to someone you like.  Friends don't let
 * friends use blink.  On friends.  You know what I mean.  Blink is
 * like, well, it's like bursting into show tunes at a funeral.  It's
 * just not done.  Not something anyone wants.  And on those rare
 * instances where it might actually be appropriate, it's still wrong,
 * since it's likely done by the wrong person for the wrong reason.
 * Just like blink.  And if I implemented blink, I'd be like a funeral
 * director who adds "Would you like us to burst into show tunes?" on
 * the list of questions asked while making funeral arrangements.
 * It's formalizing wrongness in the wrong way.  And we're just too
 * civilized to do that.  Hhhmph!
 */
#define XO_EFF_RESET		(1<<0)
#define XO_EFF_NORMAL		(1<<1)
#define XO_EFF_BOLD		(1<<2)
#define XO_EFF_UNDERLINE	(1<<3)
#define XO_EFF_INVERSE		(1<<4)

#define XO_EFF_CLEAR_BITS XO_EFF_RESET /* Reset gets reset, surprisingly */

typedef uint8_t xo_effect_t;
typedef struct xo_colors_s {
    xo_effect_t xoc_effects;	/* Current effect set */
    xo_color_t xoc_col_fg;	/* Foreground color */
    xo_color_t xoc_col_bg;	/* Background color */
} xo_colors_t;

/*
 * xo_handle_t: this is the principle data structure for libxo.
 * It's used as a store for state, options, content, and all manor
 * of other information.
 */
struct xo_handle_s {
    xo_xof_flags_t xo_flags;	/* Flags (XOF_*) from the user*/
    xo_xof_flags_t xo_iflags;	/* Internal flags (XOIF_*) */
    xo_style_t xo_style;	/* XO_STYLE_* value */
    unsigned short xo_indent;	/* Indent level (if pretty) */
    unsigned short xo_indent_by; /* Indent amount (tab stop) */
    xo_write_func_t xo_write;	/* Write callback */
    xo_close_func_t xo_close;	/* Close callback */
    xo_flush_func_t xo_flush;	/* Flush callback */
    xo_formatter_t xo_formatter; /* Custom formating function */
    xo_checkpointer_t xo_checkpointer; /* Custom formating support function */
    void *xo_opaque;		/* Opaque data for write function */
    xo_buffer_t xo_data;	/* Output data */
    xo_buffer_t xo_fmt;	   	/* Work area for building format strings */
    xo_buffer_t xo_attrs;	/* Work area for building XML attributes */
    xo_buffer_t xo_predicate;	/* Work area for building XPath predicates */
    xo_stack_t *xo_stack;	/* Stack pointer */
    int xo_depth;		/* Depth of stack */
    int xo_stack_size;		/* Size of the stack */
    xo_info_t *xo_info;		/* Info fields for all elements */
    int xo_info_count;		/* Number of info entries */
    va_list xo_vap;		/* Variable arguments (stdargs) */
    char *xo_leading_xpath;	/* A leading XPath expression */
    mbstate_t xo_mbstate;	/* Multi-byte character conversion state */
    ssize_t xo_anchor_offset;	/* Start of anchored text */
    ssize_t xo_anchor_columns;	/* Number of columns since the start anchor */
    ssize_t xo_anchor_min_width; /* Desired width of anchored text */
    ssize_t xo_units_offset;	/* Start of units insertion point */
    ssize_t xo_columns;	/* Columns emitted during this xo_emit call */
#ifndef LIBXO_TEXT_ONLY
    xo_color_t xo_color_map_fg[XO_NUM_COLORS]; /* Foreground color mappings */
    xo_color_t xo_color_map_bg[XO_NUM_COLORS]; /* Background color mappings */
#endif /* LIBXO_TEXT_ONLY */
    xo_colors_t xo_colors;	/* Current color and effect values */
    xo_buffer_t xo_color_buf;	/* HTML: buffer of colors and effects */
    char *xo_version;		/* Version string */
    int xo_errno;		/* Saved errno for "%m" */
    char *xo_gt_domain;		/* Gettext domain, suitable for dgettext(3) */
    xo_encoder_func_t xo_encoder; /* Encoding function */
    xo_whiteboard_func_t xo_wb_marker; /* Function to mark whiteboard */
    void *xo_private;		/* Private data for external encoders */
#ifdef LIBXO_NEED_MAP
    char **xo_map;		/* Name mapping array */
    int xo_map_size;		/* Size (count) of xo_map[] */
    int xo_map_len;		/* Current length (count) of xo_map[] */
    xo_buffer_t xo_map_data;	/* Data values for name mapping */
#endif /* LIBXO_NEED_MAP */
#ifdef LIBXO_NEED_FILTERS
    struct xo_filter_s *xo_filters; /* Opaque data pointer */
#endif /* LIBXO_NEED_FILTERS */
    xo_xsf_flags_t xo_rb_snap;	/* Transient: parent XSF_RB_BITS before open */
};

/* Flag operations */
#define XOF_BIT_ISSET(_flag, _bit)	(((_flag) & (_bit)) ? 1 : 0)
#define XOF_BIT_SET(_flag, _bit)	do { (_flag) |= (_bit); } while (0)
#define XOF_BIT_CLEAR(_flag, _bit)	do { (_flag) &= ~(_bit); } while (0)

#define XOF_ISSET(_xop, _bit) XOF_BIT_ISSET(_xop->xo_flags, _bit)
#define XOF_SET(_xop, _bit) XOF_BIT_SET(_xop->xo_flags, _bit)
#define XOF_CLEAR(_xop, _bit) XOF_BIT_CLEAR(_xop->xo_flags, _bit)

#define XOIF_ISSET(_xop, _bit) XOF_BIT_ISSET(_xop->xo_iflags, _bit)
#define XOIF_SET(_xop, _bit) XOF_BIT_SET(_xop->xo_iflags, _bit)
#define XOIF_CLEAR(_xop, _bit) XOF_BIT_CLEAR(_xop->xo_iflags, _bit)

/* Internal flags */
#define XOIF_REORDER	XOF_BIT(0) /* Reordering fields; record field info */
#define XOIF_DIV_OPEN	XOF_BIT(1) /* A <div> is open */
#define XOIF_TOP_EMITTED XOF_BIT(2) /* The top JSON braces have been emitted */
#define XOIF_ANCHOR	XOF_BIT(3) /* An anchor is in place  */

#define XOIF_UNITS_PENDING XOF_BIT(4) /* We have a units-insertion pending */
#define XOIF_INIT_IN_PROGRESS XOF_BIT(5) /* Init of handle is in progress */
#define XOIF_MADE_OUTPUT XOF_BIT(6)	 /* Have already made output */
#ifdef LIBXO_NEED_FILTERS
#define XOIF_FILTERING	XOF_BIT(7)	 /* Actively filtering (XOF_FILTER) */
#else  /* LIBXO_NEED_FILTERS */
#define XOIF_FILTERING 0	/* Allow the compiler to trim filter code */
#endif /* LIBXO_NEED_FILTERS */

/*
 * Normal printf has width and precision, which for strings operate as
 * min and max number of columns.  But this depends on the idea that
 * one byte means one column, which UTF-8 and multi-byte characters
 * pitches on its ear.  It may take 40 bytes of data to populate 14
 * columns, but we can't go off looking at 40 bytes of data without the
 * caller's permission for fear/knowledge that we'll generate core files.
 * 
 * So we make three values, distinguishing between "max column" and
 * "number of bytes that we will inspect inspect safely" We call the
 * later "size", and make the format "%[[<min>].[[<size>].<max>]]s".
 *
 * Under the "first do no harm" theory, we default "max" to "size".
 * This is a reasonable assumption for folks that don't grok the
 * MBS/WCS/UTF-8 world, and while it will be annoying, it will never
 * be evil.
 *
 * For example, xo_emit("{:tag/%-14.14s}", buf) will make 14
 * columns of output, but will never look at more than 14 bytes of the
 * input buffer.  This is mostly compatible with printf and caller's
 * expectations.
 *
 * In contrast xo_emit("{:tag/%-14..14s}", buf) will look at however
 * many bytes (or until a NUL is seen) are needed to fill 14 columns
 * of output.  xo_emit("{:tag/%-14.*.14s}", xx, buf) will look at up
 * to xx bytes (or until a NUL is seen) in order to fill 14 columns
 * of output.
 *
 * It's fairly amazing how a good idea (handle all languages of the
 * world) blows such a big hole in the bottom of the fairly weak boat
 * that is C string handling.  The simplicity and completenesss are
 * sunk in ways we haven't even begun to understand.
 */
#define XF_WIDTH_MIN	0	/* Minimal width */
#define XF_WIDTH_SIZE	1	/* Maximum number of bytes to examine */
#define XF_WIDTH_MAX	2	/* Maximum width */
#define XF_WIDTH_NUM	3	/* Numeric fields in printf (min.size.max) */

/* Input and output string encodings */
#define XF_ENC_WIDE	1	/* Wide characters (wchar_t) */
#define XF_ENC_UTF8	2	/* UTF-8 */
#define XF_ENC_LOCALE	3	/* Current locale */

/*
 * A place to parse printf-style format flags for each field
 */
typedef struct xo_format_s {
    unsigned char xf_fc;	/* Format character */
    unsigned char xf_enc;	/* Encoding of the string (XF_ENC_*) */
    unsigned char xf_skip;	/* Skip this field */
    unsigned char xf_lflag;	/* 'l' (long) */
    unsigned char xf_hflag;;	/* 'h' (half) */
    unsigned char xf_jflag;	/* 'j' (intmax_t) */
    unsigned char xf_tflag;	/* 't' (ptrdiff_t) */
    unsigned char xf_zflag;	/* 'z' (size_t) */
    unsigned char xf_qflag;	/* 'q' (quad_t) */
    unsigned char xf_seen_minus; /* Seen a minus */
    int xf_leading_zero;	/* Seen a leading zero (zero fill)  */
    unsigned xf_dots;		/* Seen one or more '.'s */
    int xf_width[XF_WIDTH_NUM]; /* Width/precision/size numeric fields */
    unsigned xf_stars;		/* Seen one or more '*'s */
    unsigned char xf_star[XF_WIDTH_NUM]; /* Seen one or more '*'s */
} xo_format_t;

/*
 * We keep a 'default' handle to allow callers to avoid having to
 * allocate one.  Passing NULL to any of our functions will use
 * this default handle.  Most functions have a variant that doesn't
 * require a handle at all, since most output is to stdout, which
 * the default handle handles handily.
 */
static THREAD_LOCAL(xo_handle_t) xo_default_handle;
static THREAD_LOCAL(int) xo_default_inited;
static int xo_locale_inited;
static const char *xo_program;
static int xo_codeset_is_utf8;	/* Is stdout UTF-8? */
static int filter_lib_loaded;

/*
 * To allow libxo to be used in diverse environment, we allow the
 * caller to give callbacks for memory allocation.
 */
xo_realloc_func_t xo_realloc = realloc;
xo_free_func_t xo_free = free;

/* Forward declarations */
static ssize_t
xo_transition (xo_handle_t *xop, xo_xof_flags_t flags, const char *name,
	       xo_state_t new_state);

static int
xo_color_find (const char *str);

static void
xo_buf_append_div (xo_handle_t *xop, const char *class, xo_xff_flags_t flags,
		   const char *name, ssize_t nlen,
		   const char *value, ssize_t vlen,
		   const char *fmt, ssize_t flen,
		   const char *encoding, ssize_t elen);

static void
xo_anchor_clear (xo_handle_t *xop);

static int
xo_map_option (xo_handle_t *xop, const char *opts);

/*
 * xo_style is used to retrieve the current style.  When we're built
 * for "text only" mode, we use this function to drive the removal
 * of most of the code in libxo.  We return a constant and the compiler
 * happily removes the non-text code that is not longer executed.  This
 * trims our code nicely without needing to trampel perfectly readable
 * code with ifdefs.
 */
static inline xo_style_t
xo_style (xo_handle_t *xop UNUSED)
{
#ifdef LIBXO_TEXT_ONLY
    return XO_STYLE_TEXT;
#else /* LIBXO_TEXT_ONLY */
    return xop->xo_style;
#endif /* LIBXO_TEXT_ONLY */
}

/*
 * Allow the compiler to optimize out non-text-only code while
 * still compiling it.
 */
static inline int
xo_text_only (void)
{
#ifdef LIBXO_TEXT_ONLY
    return TRUE;
#else /* LIBXO_TEXT_ONLY */
    return FALSE;
#endif /* LIBXO_TEXT_ONLY */
}

/*
 * Callback to write data to a FILE pointer
 */
static xo_ssize_t
xo_write_to_file (void *opaque, const char *data)
{
    FILE *fp = (FILE *) opaque;

    return fprintf(fp, "%s", data);
}

/*
 * Callback to close a file
 */
static void
xo_close_file (void *opaque)
{
    FILE *fp = (FILE *) opaque;

    fclose(fp);
}

/*
 * Callback to flush a FILE pointer
 */
static int
xo_flush_file (void *opaque)
{
    FILE *fp = (FILE *) opaque;

    return fflush(fp);
}

static inline int
xo_str_is_const (const char *str UNUSED)
{
#if HAVE_ETEXT == 1
    const char *xo_etext = (const char *) &etext;

    return (str < xo_etext);
#else /* HAVE_ETEXT */
    return FALSE;
#endif /* HAVE_ETEXT */
}

/* Get the current stack pointer */
static inline xo_stack_t *
xo_stack_cur (xo_handle_t *xop)
{
    return &xop->xo_stack[xop->xo_depth];
}

static int
xo_depth_check (xo_handle_t *xop, int depth)
{
    xo_stack_t *xsp;

    if (depth >= xop->xo_stack_size) {
	depth += XO_DEPTH;	/* Extra room */

	xsp = xo_realloc(xop->xo_stack, sizeof(xop->xo_stack[0]) * depth);
	if (xsp == NULL) {
	    xo_failure(xop, "xo_depth_check: out of memory (%d)", depth);
	    return -1;
	}

	int old_size = xop->xo_stack_size;
	int count = depth - old_size;

	bzero(xsp + old_size, count * sizeof(*xsp));
	xop->xo_stack_size = depth;
	xop->xo_stack = xsp;

	/* bzero sets xs_rb_off/xs_key_off/xs_tag_end to 0, but XS_OFFSET_CLEAR == -1 */
	for (int i = old_size; i < depth; i++) {
	    xsp[i].xs_rb_off = XS_OFFSET_CLEAR;
	    xsp[i].xs_tag_end = XS_OFFSET_CLEAR;
	    xsp[i].xs_key_off = XS_OFFSET_CLEAR;
	}
    }

    return 0;
}

void
xo_no_setlocale (void)
{
    xo_locale_inited = 1;	/* Skip initialization */
}

/*
 * For XML, the first character of a tag cannot be numeric, but people
 * will likely not notice.  So we people-proof them by forcing a leading
 * underscore if they use invalid tags.  Note that this doesn't cover
 * all broken tags, just this fairly specific case.
 */
static const char *
xo_xml_leader_len (xo_handle_t *xop, const char *name, xo_ssize_t nlen)
{
    if (name == NULL || name[0] == '\0' || isalpha(name[0]) || name[0] == '_')
        return "";

    xo_failure(xop, "invalid XML tag name: '%.*s'", nlen, name);
    return "_";
}

static const char *
xo_xml_leader (xo_handle_t *xop, const char *name)
{
    return xo_xml_leader_len(xop, name, strlen(name));
}

/*
 * We need to decide if stdout is line buffered (_IOLBF).  Lacking a
 * standard way to decide this (e.g. getlinebuf()), we have configure
 * look to find __flbf, which glibc supported.  If not, we'll rely on
 * isatty, with the assumption that terminals are the only thing
 * that's line buffered.  We _could_ test for "steam._flags & _IOLBF",
 * which is all __flbf does, but that's even tackier.  Like a
 * bedazzled Elvis outfit on an ugly lap dog sort of tacky.  Not
 * something we're willing to do.
 */
static int
xo_is_line_buffered (FILE *stream)
{
#if HAVE___FLBF
    if (__flbf(stream))
	return 1;
#else /* HAVE___FLBF */
    if (isatty(fileno(stream)))
	return 1;
#endif /* HAVE___FLBF */
    return 0;
}

xo_filter_ops_t xo_filter_ops;	/* The global set of filter operations */

#ifdef LIBXO_NEED_FILTERS
static int
xo_load_filter_lib (xo_handle_t *xop UNUSED)
{
    if (filter_lib_loaded)
	return 0;

    xo_filter_init_func_t func;
    const char *reason = NULL;

    void *dlp = xo_dyld_open(XO_FILTERDIR, "xo_filter", "lib");
    if (dlp == NULL)
	reason = "library not found";
    else {
	func = (xo_filter_init_func_t) xo_dyld_func(dlp, XO_FILTER_INIT_FUNC);

	if (func == NULL)
	    reason = "no init function";

	else {
	    int rc = func(XO_FILTER_OPS_VERSION, &xo_filter_ops);

	    if (rc == -1)
		reason = "init failed";
	    else if (rc != XO_FILTER_OPS_VERSION)
		reason = "version mismatch";
	}
    }

    if (reason) {
	xo_warnx("could not load filter library: %s", reason);
	return -1;
    }

    filter_lib_loaded = TRUE;
    return 0;
}
#endif /* LIBXO_NEED_FILTERS */

/*
 * Used only for test jigs, so avoid dynamic loading
 */
void
xo_setup_filter_lib_test (int version, xo_filter_ops_t *ops)
{
    if (version == XO_FILTER_OPS_VERSION) {
	memcpy(&xo_filter_ops, ops, sizeof(*ops));
	filter_lib_loaded = TRUE;
    }
}

/*
 * Initialize an xo_handle_t, using both static defaults and
 * the global settings from the LIBXO_OPTIONS environment
 * variable.
 */
static void
xo_init_handle (xo_handle_t *xop)
{
    xop->xo_opaque = stdout;
    xop->xo_write = xo_write_to_file;
    xop->xo_flush = xo_flush_file;

    if (xo_is_line_buffered(stdout))
	XOF_SET(xop, XOF_FLUSH_LINE);

    /*
     * We need to initialize the locale, which isn't really pretty.
     * Libraries should depend on their caller to set up the
     * environment.  But we really can't count on the caller to do
     * this, because well, they won't.  Trust me.
     */
    if (!xo_locale_inited) {
	xo_locale_inited = 1;	/* Only do this once */

#ifdef __FreeBSD__		/* Who does The Right Thing */
	const char *cp = "";
#else /* __FreeBSD__ */
	const char *cp = getenv("LC_ALL");
	if (cp == NULL)
	    cp = getenv("LC_CTYPE");
	if (cp == NULL)
	    cp = getenv("LANG");
	if (cp == NULL)
	    cp = "C";		/* Default for C programs */
#endif /* __FreeBSD__ */

	(void) setlocale(LC_CTYPE, cp);

#ifdef CODESET
	/* Now that locale is set, determine if our stdout output is UTF-8 */
	const char *codeset = nl_langinfo(CODESET);
	if (codeset && xo_streq(codeset, "UTF-8"))
	    xo_codeset_is_utf8 = TRUE;
#endif /* CODESET */
    }

    /*
     * Initialize only the xo_buffers we know we'll need; the others
     * can be allocated as needed.
     */
    xo_buf_init(&xop->xo_data);
    xo_buf_init(&xop->xo_fmt);

    if (XOIF_ISSET(xop, XOIF_INIT_IN_PROGRESS))
	return;
    XOIF_SET(xop, XOIF_INIT_IN_PROGRESS);

    xop->xo_indent_by = XO_INDENT_BY;
    xo_depth_check(xop, XO_DEPTH);

    XOIF_CLEAR(xop, XOIF_INIT_IN_PROGRESS);
}

static void
xo_default_init_utf8 (xo_handle_t *xop)
{
    if (xo_codeset_is_utf8)
       XOF_SET(xop, XOF_UTF8);
}

/*
 * Initialize the default handle.
 */
static void
xo_default_init (void)
{
    xo_handle_t *xop = &xo_default_handle;

    xo_init_handle(xop);
    xo_default_init_utf8(xop);

    xo_default_inited = 1;
}

/*
 * Indicate if the style is an "encoding" one as opposed to a "display" one.
 */
static int
xo_style_is_encoding (xo_handle_t *xop)
{
    if (xo_style(xop) == XO_STYLE_JSON
	|| xo_style(xop) == XO_STYLE_XML
	|| xo_style(xop) == XO_STYLE_SDPARAMS
	|| xo_style(xop) == XO_STYLE_ENCODER)
	return TRUE;
    return FALSE;
}

/*
 * Is the output for this handle UTF-8?
 */
static inline int
xo_is_style_text_utf8 (xo_handle_t *xop)
{
    if (xo_style(xop) == XO_STYLE_TEXT)
	return XOF_ISSET(xop, XOF_UTF8);

    return FALSE;
}

/*
 * Cheap convenience function to return either the argument, or
 * the internal handle, after it has been initialized.  The usage
 * is:
 *    xop = xo_default(xop);
 */
static xo_handle_t *
xo_default (xo_handle_t *xop)
{
    if (xop == NULL) {
	if (xo_default_inited == 0)
	    xo_default_init();
	xop = &xo_default_handle;
    }

    return xop;
}

/*
 * A simple debugging print function, similar to psu_dbg.  Controlled by
 * the undocumented "debug" option.
 */
void
xo_dbg_v (xo_handle_t *xop UNUSED, const char *fmt UNUSED, va_list vap UNUSED)
{
#ifndef LIBXO_TEXT_ONLY
    xop = xo_default(xop);

    if (xop == NULL || !XOF_ISSET(xop, XOF_DEBUG))
	return;

    size_t len = strlen(fmt);
    char *new_fmt = alloca(len + 2);
    memcpy(new_fmt, fmt, len);
    new_fmt[len] = '\n';
    new_fmt[len + 1] = '\0';

    vfprintf(stderr, new_fmt, vap);
#endif /* LIBXO_TEXT_ONLY */
}

void
xo_dbg (xo_handle_t *xop UNUSED, const char *fmt UNUSED, ...)
{
#ifndef LIBXO_TEXT_ONLY
    xop = xo_default(xop);

    if (xop == NULL || !XOF_ISSET(xop, XOF_DEBUG))
	return;

    va_list vap;

    va_start(vap, fmt);
    xo_dbg_v(xop, fmt, vap);
    va_end(vap);
#endif /* LIBXO_TEXT_ONLY */
}

/*
 * Return the number of spaces we should be indenting.  If
 * we are pretty-printing, this is indent * indent_by.
 */
static int
xo_indent (xo_handle_t *xop)
{
    int rc = 0;

    xop = xo_default(xop);

    if (XOF_ISSET(xop, XOF_PRETTY)) {
	rc = xop->xo_indent * xop->xo_indent_by;
	if (XOIF_ISSET(xop, XOIF_TOP_EMITTED))
	    rc += xop->xo_indent_by;
    }

    return (rc > 0) ? rc : 0;
}

/*
 * Check for sufficient room in the buffer and report if it can't be
 * accommodated.
 */
static int
xo_check_for_room (xo_handle_t *xop, xo_buffer_t *xbp, int bytes)
{
    if (xo_buf_has_room(xbp, bytes))
	return 0;

    xo_failure(xop, "buffer cannot be expanded for %d bytes", bytes);
    return -1;
}

static void
xo_buf_indent (xo_handle_t *xop, int indent)
{
    xo_buffer_t *xbp = &xop->xo_data;

    if (indent <= 0)
	indent = xo_indent(xop);

    if (xo_check_for_room(xop, xbp, indent))
	return;

    memset(xbp->xb_curp, ' ', indent);
    xbp->xb_curp += indent;
}

static char xo_xml_amp[] = "&amp;";
static char xo_xml_lt[] = "&lt;";
static char xo_xml_gt[] = "&gt;";
static char xo_xml_quot[] = "&quot;";
static char xo_xml_square[] = { 0xE2, 0x96, 0xA1, 0 };

#define XO_XML_ESCAPE_BINARY_UNICODE_BASE 0xe000
#define XO_XML_ESCAPE_BINARY_UNICODE_END 0xe100
#define XO_XML_ESCAPE_BINARY_UNICODE "&#x%04x;"
#define XO_XML_ESCAPE_BINARY_UNICODE_SIZE 8

static ssize_t
xo_escape_xml (xo_handle_t *xop, xo_buffer_t *xbp,
	       ssize_t len, xo_xff_flags_t flags)
{
    ssize_t slen;
    ssize_t delta = 0;
    int lost = 0;
    char *cp, *ep, *ip;
    int attr = XOF_BIT_ISSET(flags, XFF_ATTR);
    unsigned char ch;

    for (cp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	ch = *cp;

	/* We're subtracting 2: 1 for the NUL, 1 for the char we replace */
	if (ch == '<')
	    delta += sizeof(xo_xml_lt) - 2;
	else if (ch == '>')
	    delta += sizeof(xo_xml_gt) - 2;
	else if (ch == '&')
	    delta += sizeof(xo_xml_amp) - 2;
	else if (attr && ch == '"')
	    delta += sizeof(xo_xml_quot) - 2;

	else if ((unsigned) ch >= 0x20) /* Beware of types here */
	    continue;

	else {
	    switch (ch) {
	    case '\n':
	    case '\r':
	    case '\t':
		break;

	    default:
		lost += 1;
	    }
	}
    }

    if (delta == 0 && lost == 0) /* Nothing to escape; bail */
	return len;

    int square = !lost ? 0 : (flags & XFF_ESC_SQUARE) ? 1 : 0;
    if (square)
	delta += lost * (sizeof(xo_xml_square) - 2);
    int private = !lost ? 0 : (flags & XFF_ESC_PRIVATE) ? 1 : 0;

    if (private)
	delta += lost * (XO_XML_ESCAPE_BINARY_UNICODE_SIZE - 1);
    char private_buffer[XO_XML_ESCAPE_BINARY_UNICODE_SIZE + 1];

    /* No room?  Bail, but don't append */
    if (xo_check_for_room(xop, xbp, delta))
	return 0;

    ep = xbp->xb_curp;
    cp = ep + len;
    ip = cp + delta;
    do {
	cp -= 1;
	ip -= 1;
	const char *sp = NULL;
	ch = *cp;

	if (ch == '<')
	    sp = xo_xml_lt;
	else if (ch == '>')
	    sp = xo_xml_gt;
	else if (ch == '&')
	    sp = xo_xml_amp;
	else if (attr && ch == '"')
	    sp = xo_xml_quot;
	else if ((unsigned) ch >= 0x20) /* Beware of types here */
	    *ip = ch;

	else {
	    char ih = 0;

	    switch (ch) {
	    case '\n':
	    case '\r':
	    case '\t':
		ih = ch;
		break;

	    default:
		if (square)
		    sp = xo_xml_square;
		else if (private) {
		    snprintf(private_buffer, sizeof(private_buffer),
			     XO_XML_ESCAPE_BINARY_UNICODE, (unsigned) ch);
		    sp = private_buffer;
		} else
		    ih = ' ';
	    }

	    if (ih)
		*ip = ih;
	}

	if (sp) {
	    slen = strlen(sp);
	    ip -= slen - 1;
	    memcpy(ip, sp, slen);
	}
	
    } while (cp > ep && cp != ip);

    return len + delta;
}

/*
 * The JSON Standard (RFC 7159) says:
 * char = unescaped /
 *          escape (
 *            %x22 /          ; "    quotation mark  U+0022
 *            %x5C /          ; \    reverse solidus U+005C
 *            %x2F /          ; /    solidus         U+002F
 *            %x62 /          ; b    backspace       U+0008
 *            %x66 /          ; f    form feed       U+000C
 *            %x6E /          ; n    line feed       U+000A
 *            %x72 /          ; r    carriage return U+000D
 *            %x74 /          ; t    tab             U+0009
 *            %x75 4HEXDIG )  ; uXXXX                U+XXXX
 */
static ssize_t
xo_escape_json (xo_handle_t *xop, xo_buffer_t *xbp,
		ssize_t len, xo_xff_flags_t flags)
{
    ssize_t delta = 0;
    char *cp, *ep, *ip;

    for (cp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	char ch = *cp;

	if (ch == '\\' || ch == '"')
	    delta += 1;
	else if (ch == '/' && (flags & XFF_ESC_SLASH))
	    delta += 1;
	else if ((unsigned) ch >= 0x20) { /* Beware of types here */
	    /* nothing */

	} else {
	    switch (ch) {
	    case '\b':
	    case '\f':
	    case '\n':
	    case '\r':
	    case '\t':
		delta += 1;
		break;

	    default:
		delta += 5;
	    }
	}
    }

    if (delta == 0)		/* Nothing to escape; bail */
	return len;

    /* No room?  Bail, but don't append */
    if (xo_check_for_room(xop, xbp, delta))
	return 0;

    ep = xbp->xb_curp;
    cp = ep + len;
    ip = cp + delta;
    do {
	cp -= 1;
	ip -= 1;

	unsigned char ch = *cp;

	if (ch == '\\' || ch == '"') {
	    *ip-- = *cp;
	    *ip = '\\';

	} else if (ch == '/' && (flags & XFF_ESC_SLASH)) {
	    *ip-- = *cp;
	    *ip = '\\';

	} else if ((unsigned) ch >= 0x20) { /* Beware of types here */
	    *ip = *cp;

	} else {
	    char ih = 0;

	    switch (ch) {
	    case '\b':
		ih = 'b';
		break;

	    case '\f':
		ih = 'f';
		break;

	    case '\n':
		ih = 'n';
		break;

	    case '\r':
		ih = 'r';
		break;

	    case '\t':
		ih = 't';
		break;

	    case '\0':
		ip += 1;	/* Undo the move */
		continue;

	    default:
		{
		    static const char hexstr[] = "0123456789abcdef";
		    *ip-- = hexstr[ch & 0x0F];
		    *ip-- = hexstr[(ch >> 4) & 0x0F];
		    *ip-- = '0';
		    *ip-- = '0';
		    *ip-- = 'u';
		    *ip = '\\';
		}
	    }

	    if (ih) {
		*ip-- = ih;
		*ip = '\\';
	    }
	}
	
    } while (cp > ep && cp != ip);

    return len + delta;
}

/*
 * PARAM-VALUE     = UTF-8-STRING ; characters '"', '\' and
 *                                ; ']' MUST be escaped.
 */
static ssize_t
xo_escape_sdparams (xo_handle_t *xop, xo_buffer_t *xbp,
		    ssize_t len, xo_xff_flags_t flags UNUSED)
{
    ssize_t delta = 0;
    char *cp, *ep, *ip;

    for (cp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	if (*cp == '\\' || *cp == '"' || *cp == ']')
	    delta += 1;
    }

    if (delta == 0)		/* Nothing to escape; bail */
	return len;

    if (xo_check_for_room(xop, xbp, delta)) /* No room; bail, but don't append */
	return 0;

    ep = xbp->xb_curp;
    cp = ep + len;
    ip = cp + delta;
    do {
	cp -= 1;
	ip -= 1;

	if (*cp == '\\' || *cp == '"' || *cp == ']') {
	    *ip-- = *cp;
	    *ip = '\\';
	} else {
	    *ip = *cp;
	}
	
    } while (cp > ep && cp != ip);

    return len + delta;
}

static void
xo_buf_escape (xo_handle_t *xop, xo_buffer_t *xbp,
	       const char *str, ssize_t len, xo_xff_flags_t flags)
{
    if (xo_check_for_room(xop, xbp, len))
	return;

    memcpy(xbp->xb_curp, str, len);

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
    case XO_STYLE_HTML:
	len = xo_escape_xml(xop, xbp, len, flags);
	break;

    case XO_STYLE_JSON:
	len = xo_escape_json(xop, xbp, len, flags);
	break;

    case XO_STYLE_SDPARAMS:
	len = xo_escape_sdparams(xop, xbp, len, flags);
	break;
    }

    xbp->xb_curp += len;
}

/*
 * Write the current contents of the data buffer using the handle's
 * xo_write function.
 */
static ssize_t
xo_write (xo_handle_t *xop)
{
    ssize_t rc = 0;
    xo_buffer_t *xbp = &xop->xo_data;

    if (xbp->xb_curp != xbp->xb_bufp) {
	xo_buf_append(xbp, "", 1); /* Append ending NUL */
	xo_anchor_clear(xop);
	if (xop->xo_write)
	    rc = xop->xo_write(xop->xo_opaque, xbp->xb_bufp);

	xo_buf_reset(xbp);

	/* We have now official made output */
	XOIF_SET(xop, XOIF_MADE_OUTPUT);
    }

    /* Turn off the flags that don't survive across writes */
    XOIF_CLEAR(xop, XOIF_UNITS_PENDING);

    return rc;
}

/*
 * Format arguments into our buffer.  If a custom formatter has been set,
 * we use that to do the work; otherwise we vsnprintf().
 */
static ssize_t
xo_vsnprintf (xo_handle_t *xop, xo_buffer_t *xbp, const char *fmt, va_list vap)
{
    va_list va_local;
    ssize_t rc;
    ssize_t left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);

    va_copy(va_local, vap);

    if (xop->xo_formatter)
	rc = xop->xo_formatter(xop, xbp->xb_curp, left, fmt, va_local);
    else
	rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);

    if (rc >= left) {
	if (xo_check_for_room(xop, xbp, rc)) {
	    va_end(va_local);
	    return -1;
	}

	/*
	 * After we call vsnprintf(), the stage of vap is not defined.
	 * We need to copy it before we pass.  Then we have to do our
	 * own logic below to move it along.  This is because the
	 * implementation can have va_list be a pointer (bsd) or a
	 * structure (macosx) or anything in between.
	 */

	va_end(va_local);	/* Reset vap to the start */
	va_copy(va_local, vap);

	left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	if (xop->xo_formatter)
	    rc = xop->xo_formatter(xop, xbp->xb_curp, left, fmt, va_local);
	else
	    rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);
    }
    va_end(va_local);

    return rc;
}

/*
 * Print some data through the handle.
 */
static ssize_t
xo_printf_v (xo_handle_t *xop, const char *fmt, va_list vap)
{
    xo_buffer_t *xbp = &xop->xo_data;
    ssize_t left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
    ssize_t rc;
    va_list va_local;

    va_copy(va_local, vap);

    rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);

    if (rc >= left) {
	if (xo_check_for_room(xop, xbp, rc)) {
	    va_end(va_local);
	    return -1;
	}

	va_end(va_local);	/* Reset vap to the start */
	va_copy(va_local, vap);

	left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	rc = vsnprintf(xbp->xb_curp, left, fmt, va_local);
    }

    va_end(va_local);

    if (rc > 0)
	xbp->xb_curp += rc;

    return rc;
}

static ssize_t
xo_printf (xo_handle_t *xop, const char *fmt, ...)
{
    ssize_t rc;
    va_list vap;

    va_start(vap, fmt);

    rc = xo_printf_v(xop, fmt, vap);

    va_end(vap);
    return rc;
}

static ssize_t
xo_buf_utf8_len (xo_handle_t *xop, const char *buf, ssize_t bufsiz)
{
    unsigned b = (unsigned char) *buf;
    ssize_t len;

    len = xo_utf8_rlen(*buf);
    if (len < 0) {
        xo_failure(xop, "invalid UTF-8 data: %02hhx", b);
	return -1;
    }

    if (len > bufsiz) {
        xo_failure(xop, "invalid UTF-8 data (short): %02hhx (%d/%d)",
		   b, len, bufsiz);
	return -1;
    }

    return len;
}

/*
 * Append a single UTF-8 character to a buffer, converting it to locale
 * encoding.  Returns the number of columns consumed by that character,
 * as best we can determine it.
 */
static ssize_t
xo_buf_append_locale_from_utf8 (xo_handle_t *xop, xo_buffer_t *xbp,
				const char *ibuf, ssize_t ilen)
{
    xo_codepoint_t wc;
    ssize_t len;

    /*
     * Build our wide character from the input buffer; the number of
     * bits we pull off the first character is dependent on the length,
     * but we put 6 bits off all other bytes.
     */
    wc = xo_utf8_codepoint(ibuf, ilen, ilen, 0);
    if (xo_utf8_iserror(wc)) {
	xo_failure(xop, "invalid UTF-8 byte sequence");
	return 0;
    }

    if (XOF_ISSET(xop, XOF_NO_LOCALE)) {
	if (xo_check_for_room(xop, xbp, ilen))
	    return 0;

	memcpy(xbp->xb_curp, ibuf, ilen);
	xbp->xb_curp += ilen;

    } else {
	if (xo_check_for_room(xop, xbp, MB_LEN_MAX + 1))
	    return 0;

	bzero(&xop->xo_mbstate, sizeof(xop->xo_mbstate));
	len = wcrtomb(xbp->xb_curp, wc, &xop->xo_mbstate);

	if (len <= 0) {
	    xo_failure(xop, "could not convert wide char: %lx",
		       (unsigned long) wc);
	    return 0;
	}
	xbp->xb_curp += len;
    }

    return xo_wcwidth(wc);
}

/*
 * Append a UTF-8 string to a buffer, converting it into locale encoding
 */
static void
xo_buf_append_locale (xo_handle_t *xop, xo_buffer_t *xbp,
		      const char *cp, ssize_t len)
{
    const char *sp = cp, *ep = cp + len;
    ssize_t save_off = xbp->xb_bufp - xbp->xb_curp;
    ssize_t slen;
    int cols = 0;

    for ( ; cp < ep; cp++) {
	if (!xo_is_utf8_byte(*cp)) {
	    cols += 1;
	    continue;
	}

	/*
	 * We're looking at a non-ascii UTF-8 character.
	 * First we copy the previous data.
	 * Then we need find the length and validate it.
	 * Then we turn it into a wide string.
	 * Then we turn it into a localized string.
	 * Then we repeat.  Isn't i18n fun?
	 */
	if (sp != cp)
	    xo_buf_append(xbp, sp, cp - sp); /* Append previous data */

	slen = xo_buf_utf8_len(xop, cp, ep - cp);
	if (slen <= 0) {
	    /* Bad data; back it all out */
	    xo_buf_set_offset(xbp, save_off);
	    return;
	}

	cols += xo_buf_append_locale_from_utf8(xop, xbp, cp, slen);

	/* Next time through, we'll start at the next character */
	cp += slen - 1;
	sp = cp + 1;
    }

    /* Update column values */
    if (XOF_ISSET(xop, XOF_COLUMNS))
	xop->xo_columns += cols;
    if (XOIF_ISSET(xop, XOIF_ANCHOR))
	xop->xo_anchor_columns += cols;

    /* Before we fall into the basic logic below, we need reset len */
    len = ep - sp;
    if (len != 0) /* Append trailing data */
	xo_buf_append(xbp, sp, len);
}

/*
 * Append the given string to the given buffer, without escaping or
 * character set conversion.  This is the straight copy to the data
 * buffer with no fanciness.
 */
static void
xo_data_append (xo_handle_t *xop, const char *str, ssize_t len)
{
    xo_buf_append(&xop->xo_data, str, len);
}

/*
 * Append the given string to the given buffer
 */
static void
xo_data_escape (xo_handle_t *xop, const char *str, ssize_t len)
{
    xo_buf_escape(xop, &xop->xo_data, str, len, 0);
}

#ifdef LIBXO_NO_RETAIN
/*
 * Empty implementations of the retain logic
 */

void
xo_retain_clear_all (void)
{
    return;
}

void
xo_retain_clear (const char *fmt UNUSED)
{
    return;
}
static void
xo_retain_add (const char *fmt UNUSED, xo_field_info_t *fields UNUSED,
		unsigned num_fields UNUSED)
{
    return;
}

static int
xo_retain_find (const char *fmt UNUSED, xo_field_info_t **valp UNUSED,
		 unsigned *nump UNUSED)
{
    return -1;
}

unsigned long
xo_retain_get_hits (void)
{
    return 0;
}

#else /* !LIBXO_NO_RETAIN */
/*
 * Retain: We retain parsed field definitions to enhance performance,
 * especially inside loops.  We depend on the caller treating the format
 * strings as immutable, so that we can retain pointers into them.  We
 * hold the pointers in a hash table, so allow quick access.  Retained
 * information is retained until xo_retain_clear is called.
 */

/*
 * xo_retain_entry_t holds information about one retained set of
 * parsed fields.
 */
typedef struct xo_retain_entry_s {
    struct xo_retain_entry_s *xre_next; /* Pointer to next (older) entry */
    unsigned long xre_hits;		 /* Number of times we've hit */
    const char *xre_format;		 /* Pointer to format string */
    unsigned xre_num_fields;		 /* Number of fields saved */
    xo_field_info_t *xre_fields;	 /* Pointer to fields */
} xo_retain_entry_t;

/*
 * xo_retain_t holds a complete set of parsed fields as a hash table.
 */
#ifndef XO_RETAIN_SIZE
#define XO_RETAIN_SIZE 6
#endif /* XO_RETAIN_SIZE */
#define RETAIN_HASH_SIZE (1<<XO_RETAIN_SIZE)

typedef struct xo_retain_s {
    xo_retain_entry_t *xr_bucket[RETAIN_HASH_SIZE];
} xo_retain_t;

static THREAD_LOCAL(xo_retain_t) xo_retain;
static THREAD_LOCAL(unsigned) xo_retain_count;
static THREAD_LOCAL(unsigned long) xo_retain_hits;

/*
 * Simple hash function based on Thomas Wang's paper.  The original is
 * gone, but an archive is available on the Way Back Machine:
 *
 * http://web.archive.org/web/20071223173210/\
 *     http://www.concentric.net/~Ttwang/tech/inthash.htm
 *
 * For our purposes, we can assume the low four bits are uninteresting
 * since any string less that 16 bytes wouldn't be worthy of
 * retaining.  We toss the high bits also, since these bits are likely
 * to be common among constant format strings.  We then run Wang's
 * algorithm, and cap the result at RETAIN_HASH_SIZE.
 */
static unsigned
xo_retain_hash (const char *fmt)
{
    volatile uintptr_t iptr = (uintptr_t) (const void *) fmt;

    /* Discard low four bits and high bits; they aren't interesting */
    uint32_t val = (uint32_t) ((iptr >> 4) & (((1 << 24) - 1)));

    val = (val ^ 61) ^ (val >> 16);
    val = val + (val << 3);
    val = val ^ (val >> 4);
    val = val * 0x3a8f05c5;	/* My large prime number */
    val = val ^ (val >> 15);
    val &= RETAIN_HASH_SIZE - 1;

    return val;
}	

/*
 * Walk all buckets, clearing all retained entries
 */
void
xo_retain_clear_all (void)
{
    int i;
    xo_retain_entry_t *xrep, *next;

    for (i = 0; i < RETAIN_HASH_SIZE; i++) {
	for (xrep = xo_retain.xr_bucket[i]; xrep; xrep = next) {
	    next = xrep->xre_next;
	    xo_free(xrep);
	}
	xo_retain.xr_bucket[i] = NULL;
    }
    xo_retain_count = 0;
    xo_retain_hits = 0;
}

/*
 * Walk all buckets, clearing all retained entries
 */
void
xo_retain_clear (const char *fmt)
{
    xo_retain_entry_t **xrepp;
    unsigned hash = xo_retain_hash(fmt);

    for (xrepp = &xo_retain.xr_bucket[hash]; *xrepp;
	 xrepp = &(*xrepp)->xre_next) {
	if ((*xrepp)->xre_format == fmt) {
	    *xrepp = (*xrepp)->xre_next;
	    xo_retain_count -= 1;
	    return;
	}
    }
}

/*
 * Search the hash for an entry matching 'fmt'; return it's fields.
 */
static int
xo_retain_find (const char *fmt, xo_field_info_t **valp, unsigned *nump)
{
    if (xo_retain_count == 0)
	return -1;

    unsigned hash = xo_retain_hash(fmt);
    xo_retain_entry_t *xrep;

    for (xrep = xo_retain.xr_bucket[hash]; xrep != NULL;
	 xrep = xrep->xre_next) {
	if (xrep->xre_format == fmt) {
	    *valp = xrep->xre_fields;
	    *nump = xrep->xre_num_fields;
	    xrep->xre_hits += 1;
	    xo_retain_hits += 1;
	    return 0;
	}
    }

    return -1;
}

static void
xo_retain_add (const char *fmt, xo_field_info_t *fields, unsigned num_fields)
{
    unsigned hash = xo_retain_hash(fmt);
    xo_retain_entry_t *xrep;
    ssize_t sz = sizeof(*xrep) + (num_fields + 1) * sizeof(*fields);
    xo_field_info_t *xfip;

    xrep = xo_realloc(NULL, sz);
    if (xrep == NULL)
	return;

    xfip = (xo_field_info_t *) &xrep[1];
    memcpy(xfip, fields, num_fields * sizeof(*fields));

    bzero(xrep, sizeof(*xrep));

    xrep->xre_format = fmt;
    xrep->xre_fields = xfip;
    xrep->xre_num_fields = num_fields;

    /* Record the field info in the retain bucket */
    xrep->xre_next = xo_retain.xr_bucket[hash];
    xo_retain.xr_bucket[hash] = xrep;
    xo_retain_count += 1;
}

unsigned long
xo_retain_get_hits (void)
{
    return xo_retain_hits;
}

#endif /* !LIBXO_NO_RETAIN */

/*
 * The "warn" flag has nothing to do with the "warn" function.  This
 * flag tells libxo to report mistakes in the calling code that are
 * important to the developer but not-a-all to the user.  xo_failure()
 * is the driver for this.
 */

/* Flags for xo_warn_hcfv() */
typedef unsigned xo_warn_flags_t;
#define XO_XWF_CHECK_WARN	(1<<0) /* Check for warning flag */
#define XO_XWF_NO_EXTERR	(1<<1) /* Don't report extended error */

/*
 * Generate a warning.  Normally, this is a text message written to
 * standard error.  If the XOF_WARN_XML flag is set, then we generate
 * XMLified content on standard output.
 */
static void
xo_warn_hcfv (xo_handle_t *xop, int code, xo_warn_flags_t flags,
	     const char *fmt, va_list vap)
{
    xop = xo_default(xop);
    if ((flags & XO_XWF_CHECK_WARN) && !XOF_ISSET(xop, XOF_WARN))
	return;

    if (fmt == NULL)
	return;

    char exterr[UEXTERROR_MAXLEN] XO_UNUSED; /* The optimizer will remove */
    int extstatus = -1;

#ifdef HAVE_EXTERR
    if (!(flags & XO_XWF_NO_EXTERR))
	extstatus = uexterr_gettext(exterr, sizeof(exterr));
#endif /* HAVE_EXTERR */
    

    ssize_t len = strlen(fmt);
    ssize_t plen = xo_program ? strlen(xo_program) : 0;
    char *newfmt = alloca(len + 1 + plen + 2); /* NUL, and ": " */

    if (plen) {
	memcpy(newfmt, xo_program, plen);
	newfmt[plen++] = ':';
	newfmt[plen++] = ' ';
    }

    memcpy(newfmt + plen, fmt, len);
    newfmt[len + plen] = '\0';

    if (XOF_ISSET(xop, XOF_WARN_XML)) {
	static char err_open[] = "<error>";
	static char err_close[] = "</error>";
	static char msg_open[] = "<message>";
	static char msg_close[] = "</message>";

	xo_buffer_t *xbp = &xop->xo_data;

	xo_buf_append(xbp, err_open, sizeof(err_open) - 1);
	xo_buf_append(xbp, msg_open, sizeof(msg_open) - 1);

	va_list va_local;
	va_copy(va_local, vap);

	ssize_t left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	ssize_t rc = vsnprintf(xbp->xb_curp, left, newfmt, vap);

	if (rc >= left) {
	    if (xo_check_for_room(xop, xbp, rc)) {
		va_end(va_local);
		return;
	    }

	    va_end(vap);	/* Reset vap to the start */
	    va_copy(vap, va_local);

	    left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	    rc = vsnprintf(xbp->xb_curp, left, fmt, vap);
	}

	va_end(va_local);

	rc = xo_escape_xml(xop, xbp, rc, 1);
	xbp->xb_curp += rc;

	xo_buf_append(xbp, msg_close, sizeof(msg_close) - 1);
	xo_buf_append(xbp, err_close, sizeof(err_close) - 1);

	if (code >= 0) {
	    const char *msg = strerror(code);

	    if (msg) {
		xo_buf_append(xbp, ": ", 2);
		xo_buf_append(xbp, msg, strlen(msg));
	    }

	    if (extstatus == 0 && exterr[0] != '\0')
		xo_buf_append(xbp, exterr, strlen(exterr));
	}

	xo_buf_append(xbp, "\n", 1); /* Append newline and NUL to string */
	(void) xo_write(xop);

    } else {
	vfprintf(stderr, newfmt, vap);
	if (code >= 0) {
	    const char *msg = strerror(code);

	    if (msg)
		fprintf(stderr, ": %s", msg);

	    if (extstatus == 0 && exterr[0] != '\0')
		fprintf(stderr, " (%s)", exterr);
	}

	fprintf(stderr, "\n");
    }
}

/*
 * Generate a warning.  Normally, this is a text message written to
 * standard error.  If the XOF_WARN_XML flag is set, then we generate
 * XMLified content on standard output.
 */
void
xo_warn_hcv (xo_handle_t *xop, int code, int check_warn,
	     const char *fmt, va_list vap)
{
    xo_warn_hcfv(xop, code, check_warn ? XO_XWF_CHECK_WARN : 0, fmt, vap);
}

void
xo_warn_hc (xo_handle_t *xop, int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(xop, code, 0, fmt, vap);
    va_end(vap);
}

void
xo_warn_c (int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(NULL, code, 0, fmt, vap);
    va_end(vap);
}

void
xo_warn (const char *fmt, ...)
{
    int code = errno;
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(NULL, code, 0, fmt, vap);
    va_end(vap);
}

void
xo_warnx (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(NULL, -1, 0, fmt, vap);
    va_end(vap);
}

void
xo_err (int eval, const char *fmt, ...)
{
    int code = errno;
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(NULL, code, 0, fmt, vap);
    va_end(vap);
    xo_finish();
    exit(eval);
}

void
xo_errx (int eval, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(NULL, -1, 0, fmt, vap);
    va_end(vap);
    xo_finish();
    exit(eval);
}

void
xo_errc (int eval, int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcfv(NULL, code, XO_XWF_NO_EXTERR, fmt, vap);
    va_end(vap);
    xo_finish();
    exit(eval);
}

/*
 * Generate a warning.  Normally, this is a text message written to
 * standard error.  If the XOF_WARN_XML flag is set, then we generate
 * XMLified content on standard output.
 */
void
xo_message_hcv (xo_handle_t *xop, int code, const char *fmt, va_list vap)
{
    static char msg_open[] = "<message>";
    static char msg_close[] = "</message>";
    xo_buffer_t *xbp;
    ssize_t rc;
    va_list va_local;

    xop = xo_default(xop);

    if (fmt == NULL || *fmt == '\0')
	return;

    int need_nl = (fmt[strlen(fmt) - 1] != '\n');

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	xbp = &xop->xo_data;
	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_buf_indent(xop, xop->xo_indent_by);
	xo_buf_append(xbp, msg_open, sizeof(msg_open) - 1);

	va_copy(va_local, vap);

	ssize_t left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);

	rc = vsnprintf(xbp->xb_curp, left, fmt, vap);
	if (rc >= left) {
	    if (xo_check_for_room(xop, xbp, rc)) {
		va_end(va_local);
		return;
	    }

	    va_end(vap);	/* Reset vap to the start */
	    va_copy(vap, va_local);

	    left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
	    rc = vsnprintf(xbp->xb_curp, left, fmt, vap);
	}

	va_end(va_local);

	rc = xo_escape_xml(xop, xbp, rc, 0);
	xbp->xb_curp += rc;

	if (need_nl && code > 0) {
	    const char *msg = strerror(code);

	    if (msg) {
		xo_buf_append(xbp, ": ", 2);
		xo_buf_append(xbp, msg, strlen(msg));
	    }
	}

	if (need_nl)
	    xo_buf_append(xbp, "\n", 1); /* Append newline and NUL to string */

	xo_buf_append(xbp, msg_close, sizeof(msg_close) - 1);

	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_buf_append(xbp, "\n", 1); /* Append newline and NUL to string */

	(void) xo_write(xop);
	break;

    case XO_STYLE_HTML:
	{
	    char buf[BUFSIZ], *bp = buf, *cp;
	    ssize_t bufsiz = sizeof(buf);
	    ssize_t rc2;

	    va_copy(va_local, vap);

	    rc = vsnprintf(bp, bufsiz, fmt, va_local);
	    if (rc >= bufsiz) {
		bufsiz = rc + BUFSIZ;
		bp = xo_realloc(NULL, bufsiz);
		if (bp == NULL)
		    return;

		va_end(va_local);
		va_copy(va_local, vap);
		rc = vsnprintf(bp, bufsiz, fmt, va_local);
	    }

	    va_end(va_local);
	    cp = bp + rc;

	    if (need_nl) {
		rc2 = snprintf(cp, bufsiz - rc, "%s%s\n",
			       (code > 0) ? ": " : "",
			       (code > 0) ? strerror(code) : "");
		if (rc2 > 0)
		    rc += rc2;
	    }

	    xo_buf_append_div(xop, "message", 0, NULL, 0, bp, rc,
			      NULL, 0, NULL, 0);

	    if (bp != buf)
		xo_free(bp);
	}
	break;

    case XO_STYLE_JSON:
    case XO_STYLE_SDPARAMS:
    case XO_STYLE_ENCODER:
	/* No means of representing messages */
	return;

    case XO_STYLE_TEXT:
	rc = xo_printf_v(xop, fmt, vap);
	/*
	 * XXX need to handle UTF-8 widths
	 */
	if (rc > 0) {
	    if (XOF_ISSET(xop, XOF_COLUMNS))
		xop->xo_columns += rc;
	    if (XOIF_ISSET(xop, XOIF_ANCHOR))
		xop->xo_anchor_columns += rc;
	}

	if (need_nl && code > 0) {
	    const char *msg = strerror(code);

	    if (msg) {
		xo_printf(xop, ": %s", msg);
	    }
	}
	if (need_nl)
	    xo_printf(xop, "\n");

	break;
    }

    switch (xo_style(xop)) {
    case XO_STYLE_HTML:
	if (XOIF_ISSET(xop, XOIF_DIV_OPEN)) {
	    static char div_close[] = "</div>";

	    XOIF_CLEAR(xop, XOIF_DIV_OPEN);
	    xo_data_append(xop, div_close, sizeof(div_close) - 1);

	    if (XOF_ISSET(xop, XOF_PRETTY))
		xo_data_append(xop, "\n", 1);
	}
	break;
    }

    (void) xo_flush_h(xop);
}

void
xo_message_hc (xo_handle_t *xop, int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_message_hcv(xop, code, fmt, vap);
    va_end(vap);
}

void
xo_message_c (int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_message_hcv(NULL, code, fmt, vap);
    va_end(vap);
}

void
xo_message_e (const char *fmt, ...)
{
    int code = errno;
    va_list vap;

    va_start(vap, fmt);
    xo_message_hcv(NULL, code, fmt, vap);
    va_end(vap);
}

void
xo_message (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_message_hcv(NULL, 0, fmt, vap);
    va_end(vap);
}

void
xo_failure (xo_handle_t *xop, const char *fmt, ...)
{
    if (!XOF_ISSET(xop, XOF_WARN))
	return;

    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(xop, -1, XO_XWF_CHECK_WARN | XO_XWF_NO_EXTERR, fmt, vap);
    va_end(vap);
}

void
xo_failure_filter (xo_handle_t *xop, const char *fmt, ...)
{
    if (!XOF_ISSET(xop, XOF_FILTER_WARN))
	return;

    va_list vap;

    va_start(vap, fmt);
    xo_warn_hcv(xop, -1, XO_XWF_CHECK_WARN | XO_XWF_NO_EXTERR, fmt, vap);
    va_end(vap);
}

/* Error callback bridging xo_parse_t errors to xo_failure() */
static void
xo_parse_fail_cb (void *data, const char *fmt, va_list vap)
{
    xo_handle_t *xop = data;
    if (XOF_ISSET(xop, XOF_WARN))
	xo_warn_hcfv(xop, -1, XO_XWF_CHECK_WARN | XO_XWF_NO_EXTERR, fmt, vap);
}

/* Initialize an xo_parse_t for use with a libxo handle */
static void
xo_parse_for_handle (xo_handle_t *xop, xo_parse_t *xpp)
{
    bzero(xpp, sizeof(*xpp));
    xpp->xp_realloc = xo_realloc;
    xpp->xp_free = xo_free;
    xpp->xp_error = xo_parse_fail_cb;
    xpp->xp_error_data = xop;
}

/**
 * Create a handle for use by later libxo functions.
 *
 * Note: normal use of libxo does not require a distinct handle, since
 * the default handle (used when NULL is passed) generates text on stdout.
 *
 * @param style Style of output desired (XO_STYLE_* value)
 * @param flags Set of XOF_* flags in use with this handle
 * @return Newly allocated handle
 * @see xo_destroy
 */
xo_handle_t *
xo_create (xo_style_t style, xo_xof_flags_t flags)
{
    xo_handle_t *xop = xo_realloc(NULL, sizeof(*xop));

    if (xop) {
	bzero(xop, sizeof(*xop));

	xop->xo_style = style;
	XOF_SET(xop, flags);
	xo_init_handle(xop);
	xop->xo_style = style;	/* Reset style (see LIBXO_OPTIONS) */
    }

    return xop;
}

/**
 * Create a handle that will write to the given file.  Use
 * the XOF_CLOSE_FP flag to have the file closed on xo_destroy().
 *
 * @param fp FILE pointer to use
 * @param style Style of output desired (XO_STYLE_* value)
 * @param flags Set of XOF_* flags to use with this handle
 * @return Newly allocated handle
 * @see xo_destroy
 */
xo_handle_t *
xo_create_to_file (FILE *fp, xo_style_t style, xo_xof_flags_t flags)
{
    xo_handle_t *xop = xo_create(style, flags);

    if (xop) {
	xop->xo_opaque = fp;
	xop->xo_write = xo_write_to_file;
	xop->xo_close = xo_close_file;
	xop->xo_flush = xo_flush_file;
    }

    return xop;
}

/**
 * Set the default handler to output to a file.
 *
 * @param xop libxo handle
 * @param fp FILE pointer to use
 * @return 0 on success, non-zero on failure
 */
int
xo_set_file_h (xo_handle_t *xop, FILE *fp)
{
    xop = xo_default(xop);

    if (fp == NULL) {
	xo_failure(xop, "xo_set_file: NULL fp");
	return -1;
    }

    xop->xo_opaque = fp;
    xop->xo_write = xo_write_to_file;
    xop->xo_close = xo_close_file;
    xop->xo_flush = xo_flush_file;

    return 0;
}

/**
 * Set the default handler to output to a file.
 *
 * @param fp FILE pointer to use
 * @return 0 on success, non-zero on failure
 */
int
xo_set_file (FILE *fp)
{
    return xo_set_file_h(NULL, fp);
}

/**
 * Release any resources held by the handle.
 *
 * @param xop XO handle to alter (or NULL for default handle)
 */
void
xo_destroy (xo_handle_t *xop_arg)
{
    xo_handle_t *xop = xo_default(xop_arg);

    xo_flush_h(xop);

    if (xop->xo_close && XOF_ISSET(xop, XOF_CLOSE_FP))
	xop->xo_close(xop->xo_opaque);

    xo_free(xop->xo_stack);
    xo_buf_cleanup(&xop->xo_data);
    xo_buf_cleanup(&xop->xo_fmt);
    xo_buf_cleanup(&xop->xo_predicate);
    xo_buf_cleanup(&xop->xo_attrs);
    xo_buf_cleanup(&xop->xo_color_buf);

    if (xop->xo_version)
	xo_free(xop->xo_version);

    if (xop_arg == NULL) {
	bzero(&xo_default_handle, sizeof(xo_default_handle));
	xo_default_inited = 0;
    } else
	xo_free(xop);
}

/**
 * Record a new output style to use for the given handle (or default if
 * handle is NULL).  This output style will be used for any future output.
 *
 * @param xop XO handle to alter (or NULL for default handle)
 * @param style new output style (XO_STYLE_*)
 */
void
xo_set_style (xo_handle_t *xop, xo_style_t style)
{
    xop = xo_default(xop);
    xop->xo_style = style;
}

/**
 * Return the current style of a handle
 *
 * @param xop XO handle to access
 * @return The handle's current style
 */
xo_style_t
xo_get_style (xo_handle_t *xop)
{
    xop = xo_default(xop);
    return xo_style(xop);
}

/**
 * Return the XO_STYLE_* value matching a given name
 *
 * @param name String name of a style
 * @return XO_STYLE_* value
 */
static int
xo_name_to_style (const char *name)
{
    if (xo_streq(name, "xml"))
	return XO_STYLE_XML;
    else if (xo_streq(name, "json"))
	return XO_STYLE_JSON;
    else if (xo_streq(name, "encoder"))
	return XO_STYLE_ENCODER;
    else if (xo_streq(name, "text"))
	return XO_STYLE_TEXT;
    else if (xo_streq(name, "html"))
	return XO_STYLE_HTML;
    else if (xo_streq(name, "sdparams"))
	return XO_STYLE_SDPARAMS;

    return -1;
}

/* xo_flag_mapping_t and xo_name_lookup() are defined in xo_field.h/xo_field.c */

#ifdef NOT_NEEDED_YET
static const char *
xo_value_lookup (xo_flag_mapping_t *map, xo_xff_flags_t value)
{
    if (value == 0)
	return NULL;

    for ( ; map->xm_name; map++)
	if (map->xm_value == value)
	    return map->xm_name;

    return NULL;
}
#endif /* NOT_NEEDED_YET */

static xo_flag_mapping_t xo_xof_names[] = {
    { XOF_COLOR_ALLOWED, "color" },
    { XOF_COLOR, "color-force" },
    { XOF_COLUMNS, "columns" },
    { XOF_DEBUG, "debug" },
    { XOF_DTRT, "dtrt" },
    { XOF_FILTER_WARN, "filter-warn" },
    { XOF_FLUSH, "flush" },
    { XOF_FLUSH_LINE, "flush-line" },
    { XOF_IGNORE_CLOSE, "ignore-close" },
    { XOF_INFO, "info" },
    { XOF_KEYS, "keys" },
    { XOF_LOG_GETTEXT, "log-gettext" },
    { XOF_LOG_SYSLOG, "log-syslog" },
    { XOF_NO_HUMANIZE, "no-humanize" },
    { XOF_NO_LOCALE, "no-locale" },
    { XOF_RETAIN_NONE, "no-retain" },
    { XOF_NO_TOP, "no-top" },
    { XOF_NO_TOP_LEVEL, "no-top-level" },
    { XOF_NOT_FIRST, "not-first" },
    { XOF_PRETTY, "pretty" },
    { XOF_RETAIN_ALL, "retain" },
    { XOF_UNDERSCORES, "underscores" },
    { XOF_UNITS, "units" },
    { XOF_UTF8, "utf8" },
    { XOF_WARN, "warn" },
    { XOF_WARN_XML, "warn-xml" },
    { XOF_XPATH, "xpath" },
    { 0, NULL }
};

static xo_flag_mapping_t xo_option_names[] = {
    { XO_OPT_NO_COLOR, "no-color" },
    { XO_OPT_INDENT, "indent" },
    { XO_OPT_ENCODER, "encoder" },
    { XO_OPT_MAP, "map" },
    { XO_OPT_MAP_FILE, "map-file" },
    { XO_OPT_FILTER, "filter" },
    { XO_EXTERR_BRIEF, "exterr" },
    { XO_EXTERR_BRIEF, "exterr-brief" },
    { XO_EXTERR_VERBOSE, "exterr-verbose" },
    { 0, NULL }
};

/*
 * Convert string name to XOF_* flag value.
 * Not all are useful.  Or safe.  Or sane.
 */
static xo_xof_flags_t
xo_name_to_flag (const char *name)
{
    return xo_name_lookup(xo_xof_names, name, -1);
}

/**
 * Set the style of an libxo handle based on a string name
 *
 * @param xop XO handle
 * @param name String value of name
 * @return 0 on success, non-zero on failure
 */
int
xo_set_style_name (xo_handle_t *xop, const char *name)
{
    if (name == NULL)
	return -1;

    int style = xo_name_to_style(name);

    if (style < 0) {
	/* Might be a dynamically-loaded one ("csv") */

	if (*name == '@')
	    name += 1;		/* Allow the "@foo" shorthand */

	int rc = xo_encoder_init(xop, name);
	if (rc) {
	    xo_warnx("unknown style or encoder: '%s'", name);
	    return -1;
	}

	/*
	 * The style should already be set by the encoder library,
	 * when it calls xo_set_encoder() during initialization.  If
	 * it hasn't, then something might be wrong.  Worth a warning....
	 */
	if (xop->xo_style != XO_STYLE_ENCODER) {
	    xo_warnx("encoder style has not fully initialized");
	    return -1;
	}
    }

    xo_set_style(xop, style);
    return 0;
}

/*
 * Fill in the color map, based on the input string; currently unimplemented
 * Look for something like "colors=red/blue+green/yellow" as fg/bg pairs.
 */
static void
xo_set_color_map (xo_handle_t *xop, char *value)
{
    if (xo_text_only())
	return;

    char *cp, *ep, *vp, *np;
    ssize_t len = value ? strlen(value) + 1 : 0;
    int num = 1, fg, bg;

    for (cp = value, ep = cp + len - 1; cp && cp < ep && *cp; cp = np) {
	np = strchr(cp, '+');
	if (np)
	    *np++ = '\0';

	vp = strchr(cp, '/');
	if (vp)
	    *vp++ = '\0';

	fg = *cp ? xo_color_find(cp) : -1;
	bg = (vp && *vp) ? xo_color_find(vp) : -1;

#ifndef LIBXO_TEXT_ONLY
	xop->xo_color_map_fg[num] = (fg < 0) ? num : fg;
	xop->xo_color_map_bg[num] = (bg < 0) ? num : bg;
#endif /* LIBXO_TEXT_ONLY */

	if (++num >= XO_NUM_COLORS)
	    break;
    }

    /* If no color initialization happened, then we don't need the map */
    if (num > 1)
	XOF_SET(xop, XOF_COLOR_MAP);
    else
	XOF_CLEAR(xop, XOF_COLOR_MAP);

#ifndef LIBXO_TEXT_ONLY
    /* Fill in the rest of the colors with the defaults */
    for ( ; num < XO_NUM_COLORS; num++)
	xop->xo_color_map_fg[num] = xop->xo_color_map_bg[num] = num;
#endif /* LIBXO_TEXT_ONLY */
}

/*
 * Return the maximum number of arguments in the string.  We really don't
 * want to make something exact here, just a worst case thing.
 */
static int
xo_options_to_argv_count (xo_handle_t *xop UNUSED, char *buf)
{
    int count = 1;
    for (char *cp = buf; cp && *cp; cp = strchr(cp + 1, ','))
	count += 1;

    return count;
}

/*
 * Carve the input string into argv-style arguments, processing things
 * like backslashes
 */
static int
xo_options_to_argv (xo_handle_t *xop UNUSED, char *input,
		    int max_argc, char **argv)
{
    int ac = 0;
    char *cp, *fp, *sp;

    max_argc -= 1;		/* Save room for the NULL terminator */

    for (cp = fp = sp = input; cp && *cp; cp++, fp++) {
	char ch = *cp;
	switch (ch) {

	case '\\':
	    ch = *++cp;
	    break;

	case ',':
	    if (ac < max_argc)
		argv[ac++] = sp; /* Record the argument */

	    ch = '\0';	/* Terminate it */
	    sp = fp + 1;		/* This is the start of the next one */
	    break;
	}

	if (cp != fp || ch == '\0')
	    *fp = ch;	/* Copy data if needed */
    }

    *fp = '\0';	/* Force termination */

    if (sp != NULL)
	argv[ac++] = sp;
    argv[ac] = NULL;
    return ac;
}

/*
 * Parse the single-character short-hand versions of options, e.g. "XPW"
 */
static const char *
xo_set_options_single (xo_handle_t *xop, const char *input, int *results)
{
    ssize_t sz;
    int rc = 0;

    for (input++ ; *input && *input != ','; input++) {
	switch (*input) {
	case 'c':
	    XOF_SET(xop, XOF_COLOR_ALLOWED);
	    break;

	case 'f':
	    XOF_SET(xop, XOF_FLUSH);
	    break;

	case 'F':
	    XOF_SET(xop, XOF_FLUSH_LINE);
	    break;

	case 'g':
	    XOF_SET(xop, XOF_LOG_GETTEXT);
	    break;

	case 'H':
	    xop->xo_style = XO_STYLE_HTML;
	    break;

	case 'I':
	    XOF_SET(xop, XOF_INFO);
	    break;

	case 'i':
	    sz = strspn(input + 1, "0123456789");
	    if (sz > 0) {
		xop->xo_indent_by = atoi(input + 1);
		input += sz;	/* Skip value */
	    }
	    break;

	case 'J':
	    xop->xo_style = XO_STYLE_JSON;
	    break;

	case 'k':
	    XOF_SET(xop, XOF_KEYS);
	    break;

	case 'n':
	    XOF_SET(xop, XOF_NO_HUMANIZE);
	    break;

	case 'P':
	    XOF_SET(xop, XOF_PRETTY);
	    break;

	case 'T':
	    xop->xo_style = XO_STYLE_TEXT;
	    break;

	case 'U':
	    XOF_SET(xop, XOF_UNITS);
	    break;

	case 'u':
	    XOF_SET(xop, XOF_UNDERSCORES);
	    break;

	case 'W':
	    XOF_SET(xop, XOF_WARN);
	    break;

	case 'X':
	    xop->xo_style = XO_STYLE_XML;
	    break;

	case 'x':
	    XOF_SET(xop, XOF_XPATH);
	    break;

	default:
	    xo_warnx("unknown option: '%s'", input);
	    rc = -1;
	}
    }

    *results = rc;
    return input;
}

/*
 * Parse the multi-character long-hand versions of options,
 * e.g. "xml,pretty,warn"
 */
static int
xo_set_options_words (xo_handle_t *xop, int argc, char **argv)
{
    char *cp, *vp, *zp;
    int style = -1, new_style, rc = 0, final_rc = 0;
    xo_xof_flags_t new_flag;

    for (int i = 0; i < argc; i++) {
	if (rc)
	    final_rc = rc;

	cp = argv[i];

	/*
	 * "@foo" is a shorthand for "encoder=foo".  This is driven
	 * chiefly by a desire to make pluggable encoders not appear
	 * so distinct from built-in encoders.
	 */
	if (*cp == '@') {
	    vp = cp + 1;

	    if (*vp == '\0') {
		xo_warnx("missing value for encoder option");
		rc = -1;
	    } else {
		rc = xo_encoder_init(xop, vp);
		if (rc)
		    xo_warnx("error initializing encoder: %s", vp);
	    }

	    continue;
	}

	/* We allow either '=' or ':' to separate the keyword from the value */
	vp = strchr(cp, '=');
	zp = strchr(cp, ':');
	if (zp != NULL && (vp == NULL || zp < vp))
	    vp = zp;
	if (vp)
	    *vp++ = '\0';

	if (xo_streq("colors", cp)) {
	    xo_set_color_map(xop, vp);
	    continue;
	}

	/*
	 * For options, we don't allow "encoder" since we want to
	 * handle it explicitly below as "encoder=xxx".
	 */
	new_style = xo_name_to_style(cp);
	if (new_style >= 0 && new_style != XO_STYLE_ENCODER) {
	    if (style >= 0)
		xo_warnx("ignoring multiple styles: '%s'", cp);
	    else
		style = new_style;
	    continue;
	}

	new_flag = xo_name_to_flag(cp);
	if (new_flag != 0) {
	    XOF_SET(xop, new_flag);
	    continue;
	}

	xo_xof_flags_t opt = xo_name_lookup(xo_option_names, cp, -1);

	switch (opt) {
	case XO_OPT_NO_COLOR: /* Ignore colors */
	    XOF_CLEAR(xop, XOF_COLOR_ALLOWED);
	    continue;

	case XO_OPT_INDENT:	/* Indent by given number */
	    if (vp)
		xop->xo_indent_by = atoi(vp);
	    else {
		xo_warnx("missing value for indent option");
		rc = -1;
	    }
	    continue;

	case XO_OPT_ENCODER: /* Use a specific encoder */
	    if (vp == NULL) {
		xo_warnx("missing value for encoder option");
		rc = -1;
	    } else {
		rc = xo_encoder_init(xop, vp);
		if (rc)
		    xo_warnx("error initializing encoder: %s", vp);
	    }
	    continue;

	case XO_OPT_MAP: /* Map field names */
	    if (vp == NULL) {
		xo_warnx("missing value for map option");
		rc = -1;
	    } else {
		rc = xo_map_option(xop, vp);
		if (rc)
		    xo_warnx("error initializing map: '%s'", vp);
	    }
	    continue;

	case XO_OPT_MAP_FILE: /* Use file full of field names mappings */
	    if (vp == NULL) {
		xo_warnx("missing value for map-file option");
		rc = -1;
	    } else {
		rc = xo_map_add_file(xop, vp);
		if (rc)
		    xo_warnx("error initializing map-file: '%s'", vp);
	    }
	    continue;

	case XO_OPT_FILTER:	/* Filter output using path */
	    if (vp == NULL) {
		xo_warnx("missing value for filter option");
		rc = -1;
	    } else
		rc = xo_add_filter(xop, vp); /* Reports its own errors */
	    continue;

	case XO_EXTERR_BRIEF: /* Display brief extended error info */
	    setenv("EXTERROR_VERBOSE", "brief", 1);
	    continue;

	case XO_EXTERR_VERBOSE: /* Display verbose exterr info */
	    setenv("EXTERROR_VERBOSE", "verbose", 1);
	    continue;

	default:
	    xo_warnx("unknown libxo option value: '%s'", cp);
	    rc = -1;
	}
    }

    if (style >= 0)
	xop->xo_style= style;

    return final_rc ?: rc;
}

/**
 * Set the options for a handle using a string of options
 * passed in.  The input is a comma-separated set of names
 * and optional values: "xml,pretty,indent=4"
 *
 * @param xop XO handle
 * @param input Comma-separated set of option values
 * @return 0 on success, non-zero on failure
 */
int
xo_set_options (xo_handle_t *xop, const char *input)
{
    int rc = 0;

    if (input == NULL)
	return 0;

    xop = xo_default(xop);

#ifdef LIBXO_COLOR_ON_BY_DEFAULT
    /* If the installer used --enable-color-on-by-default, then we allow it */
    XOF_SET(xop, XOF_COLOR_ALLOWED);
#endif /* LIBXO_COLOR_ON_BY_DEFAULT */

    /*
     * We support a simpler, old-school style of giving option
     * also, using a single character for each option.  It's
     * ideal for lazy people, such as myself.
     */
    if (*input == ':') {
	input = xo_set_options_single(xop, input, &rc);

	/*
	 * Allow ',' to switch into word-style options ("--libxo:XPW,debug")
	 */
	if (*input != ',')
	    return rc;

	input += 1;
    }

    ssize_t len = strlen(input) + 1;
    char *bp = alloca(len);
    memcpy(bp, input, len);

    int argc = xo_options_to_argv_count(xop, bp);
    char **argv = xo_realloc(NULL, sizeof(argv[0]) * (argc + 1));
    if (argv == NULL) {
	xo_warnx("xo_set_options ran out of memory");
	return -1;
    }

    argc = xo_options_to_argv(xop, bp, argc, argv);
    if (argc < 0) {
	xo_free(argv);
	return argc;
    }

    rc = xo_set_options_words(xop, argc, argv);

    xo_free(argv);

    return rc;
}

/**
 * Set one or more flags for a given handle (or default if handle is NULL).
 * These flags will affect future output.
 *
 * @param xop XO handle to alter (or NULL for default handle)
 * @param flags Flags to be set (XOF_*)
 */
void
xo_set_flags (xo_handle_t *xop, xo_xof_flags_t flags)
{
    xop = xo_default(xop);

    XOF_SET(xop, flags);
}

/**
 * Return non-zero if any of the flags are set.
 *
 * @param xop XO handle to test (or NULL for default handle)
 * @param flags Set of flags to test (XOF_*)
 */
int
xo_isset_flags (xo_handle_t *xop, xo_xof_flags_t flags)
{
    xop = xo_default(xop);

    return (xop->xo_flags & flags) ? TRUE : FALSE;
}

/**
 * Accessor to return the current set of flags for a handle
 * @param xop XO handle
 * @return Current set of flags
 */
xo_xof_flags_t
xo_get_flags (xo_handle_t *xop)
{
    xop = xo_default(xop);

    return xop->xo_flags;
}

/**
 * strndup with a twist: len < 0 means len = strlen(str)
 */
static char *
xo_strndup (const char *str, ssize_t len)
{
    if (len < 0)
	len = strlen(str);

    char *cp = xo_realloc(NULL, len + 1);
    if (cp) {
	memcpy(cp, str, len);
	cp[len] = '\0';
    }

    return cp;
}

/**
 * Record a leading prefix for the XPath we generate.  This allows the
 * generated data to be placed within an XML hierarchy but still have
 * accurate XPath expressions.
 *
 * @param xop XO handle to alter (or NULL for default handle)
 * @param path The XPath expression
 */
void
xo_set_leading_xpath (xo_handle_t *xop, const char *path)
{
    xop = xo_default(xop);

    if (xop->xo_leading_xpath) {
	xo_free(xop->xo_leading_xpath);
	xop->xo_leading_xpath = NULL;
    }

    if (path == NULL)
	return;

    xop->xo_leading_xpath = xo_strndup(path, -1);
}

/**
 * Record the info data for a set of tags
 *
 * @param xop XO handle to alter (or NULL for default handle)
 * @param info Info data (xo_info_t) to be recorded (or NULL) (MUST BE SORTED)
 * @pararm count Number of entries in info (or -1 to count them ourselves)
 */
void
xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count)
{
    xop = xo_default(xop);

    if (count < 0 && infop) {
	xo_info_t *xip;

	for (xip = infop, count = 0; xip->xi_name; xip++, count++)
	    continue;
    }

    xop->xo_info = infop;
    xop->xo_info_count = count;
}

/**
 * Set the formatter callback for a handle.  The callback should
 * return a newly formatting contents of a formatting instruction,
 * meaning the bits inside the braces.
 */
void
xo_set_formatter (xo_handle_t *xop, xo_formatter_t func,
		  xo_checkpointer_t cfunc)
{
    xop = xo_default(xop);

    xop->xo_formatter = func;
    xop->xo_checkpointer = cfunc;
}

/**
 * Clear one or more flags for a given handle (or default if handle is NULL).
 * These flags will affect future output.
 *
 * @param xop XO handle to alter (or NULL for default handle)
 * @param flags Flags to be cleared (XOF_*)
 */
void
xo_clear_flags (xo_handle_t *xop, xo_xof_flags_t flags)
{
    xop = xo_default(xop);

    XOF_CLEAR(xop, flags);
}

static const char *
xo_state_name (xo_state_t state)
{
    static const char *names[] = {
	"init",
	"open_container",
	"close_container",
	"open_list",
	"close_list",
	"open_instance",
	"close_instance",
	"open_leaf_list",
	"close_leaf_list",
	"discarding",
	"marker",
	"emit",
	"emit_leaf_list",
	"finish",
	NULL
    };

    if (state < (sizeof(names) / sizeof(names[0])))
	return names[state];

    return "unknown";
}

static void
xo_line_ensure_open (xo_handle_t *xop, xo_xff_flags_t flags UNUSED)
{
    static char div_open[] = "<div class=\"line\">";
    static char div_open_blank[] = "<div class=\"blank-line\">";

    if (XOF_ISSET(xop, XOF_CONTINUATION)) {
	XOF_CLEAR(xop, XOF_CONTINUATION);
	XOIF_SET(xop, XOIF_DIV_OPEN);
	return;
    }

    if (XOIF_ISSET(xop, XOIF_DIV_OPEN))
	return;

    if (xo_style(xop) != XO_STYLE_HTML)
	return;

    XOIF_SET(xop, XOIF_DIV_OPEN);
    if (flags & XFF_BLANK_LINE)
	xo_data_append(xop, div_open_blank, sizeof(div_open_blank) - 1);
    else
	xo_data_append(xop, div_open, sizeof(div_open) - 1);

    if (XOF_ISSET(xop, XOF_PRETTY))
	xo_data_append(xop, "\n", 1);
}

static void
xo_line_close (xo_handle_t *xop)
{
    static char div_close[] = "</div>";

    switch (xo_style(xop)) {
    case XO_STYLE_HTML:
	if (!XOIF_ISSET(xop, XOIF_DIV_OPEN))
	    xo_line_ensure_open(xop, 0);

	XOIF_CLEAR(xop, XOIF_DIV_OPEN);
	xo_data_append(xop, div_close, sizeof(div_close) - 1);

	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_data_append(xop, "\n", 1);
	break;

    case XO_STYLE_TEXT:
	xo_data_append(xop, "\n", 1);
	break;
    }
}

static int
xo_info_compare (const void *key, const void *data)
{
    const char *name = key;
    const xo_info_t *xip = data;

    return strcmp(name, xip->xi_name);
}


static xo_info_t *
xo_info_find (xo_handle_t *xop, const char *name, ssize_t nlen)
{
    xo_info_t *xip;
    char *cp = alloca(nlen + 1); /* Need local copy for NUL termination */

    memcpy(cp, name, nlen);
    cp[nlen] = '\0';

    xip = bsearch(cp, xop->xo_info, xop->xo_info_count,
		  sizeof(xop->xo_info[0]), xo_info_compare);
    return xip;
}

#define CONVERT(_have, _need) (((_have) << 8) | (_need))

/*
 * Check to see that the conversion is safe and sane.
 */
static int
xo_check_conversion (xo_handle_t *xop, int have_enc, int need_enc)
{
    switch (CONVERT(have_enc, need_enc)) {
    case CONVERT(XF_ENC_UTF8, XF_ENC_UTF8):
    case CONVERT(XF_ENC_UTF8, XF_ENC_LOCALE):
    case CONVERT(XF_ENC_WIDE, XF_ENC_UTF8):
    case CONVERT(XF_ENC_WIDE, XF_ENC_LOCALE):
    case CONVERT(XF_ENC_LOCALE, XF_ENC_LOCALE):
    case CONVERT(XF_ENC_LOCALE, XF_ENC_UTF8):
	return 0;

    default:
	xo_failure(xop, "invalid conversion (%c:%c)", have_enc, need_enc);
	return 1;
    }
}

static int
xo_format_string_direct (xo_handle_t *xop, xo_buffer_t *xbp,
			 xo_xff_flags_t flags,
			 const wchar_t *wcp, const char *cp,
			 ssize_t len, int max,
			 int need_enc, int have_enc)
{
    int cols = 0;
    xo_codepoint_t wc = 0;
    ssize_t ilen, olen;
    ssize_t width;
    int attr = XOF_BIT_ISSET(flags, XFF_ATTR);
    const char *sp;

    if (len > 0 && xo_check_for_room(xop, xbp, len))
	return 0;

    /*
     * If we have the "right" encoding for text, then our job is
     * simpler.  We can skim over the string and process it quickly.
     */
    if (cp && len > 0 && xo_is_style_text_utf8(xop) && need_enc == have_enc) {
	const char *np, *ep;
	ssize_t clen = len < 0 ? (ssize_t) strlen(cp) : len;
	for (np = cp, ep = cp + clen; np < ep && *np; np++)
	    if (xo_is_utf8_byte(*np) || *np == '\\' || *np == '%'
		|| *np == '{' || *np == '}')
		break;

	/* If we found no non-ascii characters, we're golden */
	if (np == ep) {
	    if (xo_check_for_room(xop, xbp, clen))
		return -1;

	    memcpy(xbp->xb_curp, cp, clen);
	    xbp->xb_curp += clen;
	    return clen;		/* Len is the number of columns */
	}
    }

    for (;;) {
	if (len == 0)
	    break;

	if (cp) {
	    if (*cp == '\0')
		break;
	    if ((flags & XFF_UNESCAPE) && (*cp == '\\' || *cp == '%')) {
		cp += 1;
		len -= 1;
		if (len == 0 || *cp == '\0')
		    break;
	    }
	}

	if (wcp && *wcp == L'\0')
	    break;

	ilen = 0;

	switch (have_enc) {
	case XF_ENC_WIDE:		/* Wide character */
	    wc = *wcp++;
	    ilen = 1;
	    break;

	case XF_ENC_UTF8:		/* UTF-8 */
	    /* Optimize the simple case: this is a traditional ASCII c */
	    if (!xo_is_utf8_byte(*cp)) {
		wc = (unsigned char) *cp++;
		ilen = 1;
		break;
	    }

	    ilen = xo_utf8_rlen(*cp);
	    if (ilen < 0) {
		xo_failure(xop, "invalid UTF-8 character: %02hhx", *cp);
		return -1;	/* Can't continue; we can't find the end */
	    }

	    if (len > 0 && len < ilen) {
		len = 0;	/* Break out of the loop */
		continue;
	    }

	    wc = xo_utf8_codepoint(cp, ilen, ilen, 0);
	    if (xo_utf8_iserror(wc)) {
		xo_failure(xop, "invalid UTF-8 character: %02hhx/%d",
			   *cp, ilen);
		return -1;	/* Can't continue; we can't find the end */
	    }
	    cp += ilen;
	    break;

	case XF_ENC_LOCALE:;		/* Native locale */
	    wchar_t twc;
	    ilen = (len > 0) ? len : MB_LEN_MAX;
	    ilen = mbrtowc(&twc, cp, ilen, &xop->xo_mbstate);
	    wc = (xo_codepoint_t) twc;
	    if (ilen < 0) {		/* Invalid data; skip */
		xo_failure(xop, "invalid mbs char: %02hhx", *cp);
		wc = L'?';
		ilen = 1;
	    }

	    if (ilen == 0) {		/* Hit a wide NUL character */
		len = 0;
		continue;
	    }

	    cp += ilen;
	    break;
	}

	/* Reduce len, but not below zero */
	if (len > 0) {
	    len -= ilen;
	    if (len < 0)
		len = 0;
	}

	/*
	 * Find the width-in-columns of this character, which must be done
	 * in wide characters, since we lack a mbswidth() function.
	 */
	width = xo_wcwidth(wc);
	if (width < 0)
	    width = iswcntrl(wc) ? 0 : 1;

	if (xo_style(xop) == XO_STYLE_TEXT || xo_style(xop) == XO_STYLE_HTML) {
	    if (max > 0 && cols + width > max)
		break;
	}

	switch (need_enc) {
	case XF_ENC_UTF8:

	    /* Output in UTF-8 needs to be escaped, based on the style */
	    switch (xo_style(xop)) {
	    case XO_STYLE_XML:
	    case XO_STYLE_HTML:
		if (wc == '<')
		    sp = xo_xml_lt;
		else if (wc == '>')
		    sp = xo_xml_gt;
		else if (wc == '&')
		    sp = xo_xml_amp;
		else if (attr && wc == '"')
		    sp = xo_xml_quot;
		else
		    break;

		ssize_t slen = strlen(sp);
		if (xo_check_for_room(xop, xbp, slen - 1))
		    return -1;

		memcpy(xbp->xb_curp, sp, slen);
		xbp->xb_curp += slen;
		goto done_with_encoding; /* Need multi-level 'break' */

	    case XO_STYLE_JSON:
		if (wc != '\\' && wc != '"' && wc != '\n' && wc != '\r')
		    break;

		if (xo_check_for_room(xop, xbp, 2))
		    return -1;

		*xbp->xb_curp++ = '\\';
		if (wc == '\n')
		    wc = 'n';
		else if (wc == '\r')
		    wc = 'r';
		else wc = wc & 0x7f;

		*xbp->xb_curp++ = wc;
		goto done_with_encoding;

	    case XO_STYLE_SDPARAMS:
		if (wc != '\\' && wc != '"' && wc != ']')
		    break;

		if (xo_check_for_room(xop, xbp, 2))
		    return -1;

		*xbp->xb_curp++ = '\\';
		wc = wc & 0x7f;
		*xbp->xb_curp++ = wc;
		goto done_with_encoding;
	    }

	    olen = xo_utf8_to_len(wc);
	    if (olen < 0) {
		xo_failure(xop, "ignoring bad length");
		continue;
	    }

	    if (xo_check_for_room(xop, xbp, olen))
		return -1;

	    xo_utf8_emit_char(xbp->xb_curp, olen, wc);
	    xbp->xb_curp += olen;
	    break;

	case XF_ENC_LOCALE:
	    if (xo_check_for_room(xop, xbp, MB_LEN_MAX + 1))
		return -1;

	    olen = wcrtomb(xbp->xb_curp, wc, &xop->xo_mbstate);
	    if (olen <= 0) {
		xo_failure(xop, "could not convert wide char: %lx",
			   (unsigned long) wc);
		width = 1;
		*xbp->xb_curp++ = '?';
	    } else
		xbp->xb_curp += olen;
	    break;
	}

    done_with_encoding:
	cols += width;
    }

    return cols;
}

static int
xo_needed_encoding (xo_handle_t *xop)
{
    if (XOF_ISSET(xop, XOF_UTF8)) /* Check the override flag */
	return XF_ENC_UTF8;

    if (xo_style(xop) == XO_STYLE_TEXT) /* Text defaults to locale */
	return XF_ENC_LOCALE;

    return XF_ENC_UTF8;		/* Otherwise, we love UTF-8 */
}

static ssize_t
xo_format_string (xo_handle_t *xop, xo_buffer_t *xbp, xo_xff_flags_t flags,
		  xo_format_t *xfp)
{
    static char null[] = "(null)";
    static char null_no_quotes[] = "null";

    char *cp = NULL;
    wchar_t *wcp = NULL;
    ssize_t len;
    ssize_t cols = 0, rc = 0;
    ssize_t off = xbp->xb_curp - xbp->xb_bufp, off2;
    int need_enc = xo_needed_encoding(xop);

    if (xo_check_conversion(xop, xfp->xf_enc, need_enc))
	return 0;

    len = xfp->xf_width[XF_WIDTH_SIZE];

    if (xfp->xf_fc == 'm') {
	cp = strerror(xop->xo_errno);
	if (len < 0)
	    len = cp ? strlen(cp) : 0;
	goto normal_string;

    } else if (xfp->xf_enc == XF_ENC_WIDE) {
	wcp = va_arg(xop->xo_vap, wchar_t *);
	if (xfp->xf_skip)
	    return 0;

	/*
	 * Dont' deref NULL; use the traditional "(null)" instead
	 * of the more accurate "who's been a naughty boy, then?".
	 */
	if (wcp == NULL) {
	    cp = null;
	    len = sizeof(null) - 1;
	}

    } else {
	cp = va_arg(xop->xo_vap, char *); /* UTF-8 or native */

    normal_string:
	if (xfp->xf_skip)
	    return 0;

	/* Echo "Dont' deref NULL" logic */
	if (cp == NULL) {
	    if ((flags & XFF_NOQUOTE) && xo_style_is_encoding(xop)) {
		cp = null_no_quotes;
		len = sizeof(null_no_quotes) - 1;
	    } else {
		cp = null;
		len = sizeof(null) - 1;
	    }
	}

	/*
	 * Optimize the most common case, which is "%s".  We just
	 * need to copy the complete string to the output buffer.
	 */
	if (xfp->xf_enc == need_enc
		&& xfp->xf_width[XF_WIDTH_MIN] < 0
		&& xfp->xf_width[XF_WIDTH_SIZE] < 0
		&& xfp->xf_width[XF_WIDTH_MAX] < 0
	        && !(XOIF_ISSET(xop, XOIF_ANCHOR)
		     || XOF_ISSET(xop, XOF_COLUMNS))) {
	    len = strlen(cp);
	    xo_buf_escape(xop, xbp, cp, len, flags);

	    /*
	     * Our caller expects xb_curp left untouched, so we have
	     * to reset it and return the number of bytes written to
	     * the buffer.
	     */
	    off2 = xbp->xb_curp - xbp->xb_bufp;
	    rc = off2 - off;
	    xo_buf_set_offset(xbp, off);

	    return rc;
	}
    }

    cols = xo_format_string_direct(xop, xbp, flags, wcp, cp, len,
				   xfp->xf_width[XF_WIDTH_MAX],
				   need_enc, xfp->xf_enc);
    if (cols < 0)
	goto bail;

    /*
     * xo_buf_append* will move xb_curp, so we save/restore it.
     */
    off2 = xbp->xb_curp - xbp->xb_bufp;
    rc = off2 - off;
    xo_buf_set_offset(xbp, off);

    if (cols < xfp->xf_width[XF_WIDTH_MIN]) {
	/*
	 * Find the number of columns needed to display the string.
	 * If we have the original wide string, we just call wcswidth,
	 * but if we did the work ourselves, then we need to do it.
	 */
	int delta = xfp->xf_width[XF_WIDTH_MIN] - cols;
	if (xo_check_for_room(xop, xbp, xfp->xf_width[XF_WIDTH_MIN]))
	    goto bail;

	/*
	 * If seen_minus, then pad on the right; otherwise move it so
	 * we can pad on the left.
	 */
	if (xfp->xf_seen_minus) {
	    cp = xbp->xb_curp + rc;
	} else {
	    cp = xbp->xb_curp;
	    memmove(xbp->xb_curp + delta, xbp->xb_curp, rc);
	}

	/* Set the padding */
	memset(cp, (xfp->xf_leading_zero > 0) ? '0' : ' ', delta);
	rc += delta;
	cols += delta;
    }

    if (XOF_ISSET(xop, XOF_COLUMNS))
	xop->xo_columns += cols;
    if (XOIF_ISSET(xop, XOIF_ANCHOR))
	xop->xo_anchor_columns += cols;

    return rc;

 bail:
    xo_buf_set_offset(xbp, off);
    return 0;
}

/*
 * Look backwards in a buffer to find a numeric value
 */
static int
xo_buf_find_last_number (xo_buffer_t *xbp, ssize_t start_offset)
{
    int rc = 0;			/* Fail with zero */
    int digit = 1;
    char *sp = xbp->xb_bufp;
    char *cp = sp + start_offset;

    for (;;) {
	if (cp == sp)
	    break;
	cp -= 1;
	if (isdigit((int) *cp))
	    break;
    }

    for ( ; cp >= sp; cp--) {
	if (!isdigit((int) *cp))
	    break;
	if (rc >= INT_MAX / 100) /* Don't even get close to the limit */
	    break;
	rc += (*cp - '0') * digit;
	digit *= 10;
	if (cp == sp)		/* Avoid "cp--" */
	    break;
    }

    return rc;
}

static ssize_t
xo_count_utf8_cols (const char *str, ssize_t len)
{
    ssize_t tlen;
    xo_codepoint_t wc;
    ssize_t cols = 0;
    const char *ep = str + len;

    while (str < ep) {
	tlen = xo_utf8_rlen(*str);
	if (tlen < 0)		/* Broken input is very bad */
	    return cols;

	wc = xo_utf8_codepoint(str, tlen, tlen, 0);
	if (xo_utf8_iserror(wc))
	    return cols;

	/* We only print printable characters */
	if (iswprint((wint_t) wc)) {
	    /*
	     * Find the width-in-columns of this character, which must be done
	     * in wide characters, since we lack a mbswidth() function.
	     */
	    ssize_t width = xo_wcwidth(wc);
	    if (width < 0)
		width = iswcntrl(wc) ? 0 : 1;

	    cols += width;
	}

	str += tlen;
    }

    return cols;
}

#ifdef HAVE_GETTEXT
static inline const char *
xo_dgettext (xo_handle_t *xop, const char *str)
{
    const char *domainname = xop->xo_gt_domain;
    const char *res;

    res = dgettext(domainname, str);

    if (XOF_ISSET(xop, XOF_LOG_GETTEXT))
	fprintf(stderr, "xo: gettext: %s%s%smsgid \"%s\" returns \"%s\"\n",
		domainname ? "domain \"" : "", xo_printable(domainname),
		domainname ? "\", " : "", xo_printable(str), xo_printable(res));

    return res;
}

static inline const char *
xo_dngettext (xo_handle_t *xop, const char *sing, const char *plural,
	      unsigned long int n)
{
    const char *domainname = xop->xo_gt_domain;
    const char *res;

    res = dngettext(domainname, sing, plural, n);
    if (XOF_ISSET(xop, XOF_LOG_GETTEXT))
	fprintf(stderr, "xo: gettext: %s%s%s"
		"msgid \"%s\", msgid_plural \"%s\" (%lu) returns \"%s\"\n",
		domainname ? "domain \"" : "", 
		xo_printable(domainname), domainname ? "\", " : "",
		xo_printable(sing),
		xo_printable(plural), n, xo_printable(res));

    return res;
}
#else /* HAVE_GETTEXT */
static inline const char *
xo_dgettext (xo_handle_t *xop UNUSED, const char *str)
{
    return str;
}

static inline const char *
xo_dngettext (xo_handle_t *xop UNUSED, const char *singular,
	      const char *plural, unsigned long int n)
{
    return (n == 1) ? singular : plural;
}
#endif /* HAVE_GETTEXT */

/*
 * This is really _re_formatting, since the normal format code has
 * generated a beautiful string into xo_data, starting at
 * start_offset.  We need to see if it's plural, which means
 * comma-separated options, or singular.  Then we make the appropriate
 * call to d[n]gettext() to get the locale-based version.  Note that
 * both input and output of gettext() this should be UTF-8.
 */
static ssize_t
xo_format_gettext (xo_handle_t *xop, xo_xff_flags_t flags,
		   ssize_t start_offset, ssize_t cols, int need_enc)
{
    xo_buffer_t *xbp = &xop->xo_data;

    if (xo_check_for_room(xop, xbp, 1))
	return cols;

    xbp->xb_curp[0] = '\0'; /* NUL-terminate the input string */
    
    char *cp = xbp->xb_bufp + start_offset;
    ssize_t len = xbp->xb_curp - cp;
    const char *newstr = NULL;

    /*
     * The plural flag asks us to look backwards at the last numeric
     * value rendered and disect the string into two pieces.
     */
    if (flags & XFF_GT_PLURAL) {
	int n = xo_buf_find_last_number(xbp, start_offset);
	char *two = memchr(cp, (int) ',', len);
	if (two == NULL) {
	    xo_failure(xop, "no comma in plural gettext field: '%s'", cp);
	    return cols;
	}

	if (two == cp) {
	    xo_failure(xop, "nothing before comma in plural gettext "
		       "field: '%s'", cp);
	    return cols;
	}

	if (two == xbp->xb_curp) {
	    xo_failure(xop, "nothing after comma in plural gettext "
		       "field: '%s'", cp);
	    return cols;
	}

	*two++ = '\0';
	if (flags & XFF_GT_FIELD) {
	    newstr = xo_dngettext(xop, cp, two, n);
	} else {
	    /* Don't do a gettext() look up, just get the plural form */
	    newstr = (n == 1) ? cp : two;
	}

	/*
	 * If we returned the first string, optimize a bit by
	 * backing up over comma
	 */
	if (newstr == cp) {
	    xbp->xb_curp = two - 1; /* One for comma */
	    /*
	     * If the caller wanted UTF8, we're done; nothing changed,
	     * but we need to count the columns used.
	     */
	    if (need_enc == XF_ENC_UTF8)
		return xo_count_utf8_cols(cp, xbp->xb_curp - cp);
	}

    } else {
	/* The simple case (singular) */
	newstr = xo_dgettext(xop, cp);

	if (newstr == cp) {
	    /* If the caller wanted UTF8, we're done; nothing changed */
	    if (need_enc == XF_ENC_UTF8)
		return cols;
	}
    }

    /*
     * Since the new string string might be in gettext's buffer or
     * in the buffer (as the plural form), we make a copy.
     */
    ssize_t nlen = strlen(newstr);
    char *newcopy = alloca(nlen + 1);
    memcpy(newcopy, newstr, nlen + 1);

    xo_buf_set_offset(xbp, start_offset); /* Reset the buffer */
    return xo_format_string_direct(xop, xbp, flags, NULL, newcopy, nlen, 0,
				   need_enc, XF_ENC_UTF8);
}

static void
xo_data_append_content (xo_handle_t *xop, const char *str, ssize_t len,
			xo_xff_flags_t flags)
{
    int cols;
    int need_enc = xo_needed_encoding(xop);
    ssize_t start_offset = xo_buf_offset(&xop->xo_data);

    cols = xo_format_string_direct(xop, &xop->xo_data, XFF_UNESCAPE | flags,
				   NULL, str, len, -1,
				   need_enc, XF_ENC_UTF8);
    if (flags & XFF_GT_FLAGS)
	cols = xo_format_gettext(xop, flags, start_offset, cols, need_enc);

    if (XOF_ISSET(xop, XOF_COLUMNS))
	xop->xo_columns += cols;
    if (XOIF_ISSET(xop, XOIF_ANCHOR))
	xop->xo_anchor_columns += cols;
}

/**
 * Bump one of the 'width' values in a format strings (e.g. "%40.50.60s").
 * @param xfp Formatting instructions
 * @param digit Single digit (0-9) of input
 */
static void
xo_bump_width (xo_format_t *xfp, int digit)
{
    int *ip = &xfp->xf_width[xfp->xf_dots];

    *ip = ((*ip > 0) ? *ip : 0) * 10 + digit;
}

static ssize_t
xo_trim_ws (xo_buffer_t *xbp, ssize_t len)
{
    char *cp, *sp, *ep;
    ssize_t delta;

    /* First trim leading space */
    for (cp = sp = xbp->xb_curp, ep = cp + len; cp < ep; cp++) {
	if (*cp != ' ')
	    break;
    }

    delta = cp - sp;
    if (delta) {
	len -= delta;
	memmove(sp, cp, len);
    }

    /* Then trim off the end */
    for (cp = xbp->xb_curp, sp = ep = cp + len; cp < ep; ep--) {
	if (ep[-1] != ' ')
	    break;
    }

    delta = sp - ep;
    if (delta) {
	len -= delta;
	cp[len] = '\0';
    }

    return len;
}

/*
 * Pull a "long double" off our va_list.  This was originally done as
 * a function to hide some evil bits we had to do to work around a
 * gcc-4.9 bug, but that's ancient so we've removed the evil but left
 * the function just in case the bug returns.  A pessimist, eh?
 */
static inline void
xo_safe_va_arg_long_double (xo_handle_t *xop)
{
    va_arg(xop->xo_vap, long double);
}

/*
 * Flush a run of literal (non-format) characters to the output buffer,
 * updating column counts as needed.
 */
static ssize_t
xo_flush_literal (xo_handle_t *xop, xo_buffer_t *xbp, xo_xff_flags_t flags,
		  int make_output, int need_enc, const char *xp, ssize_t len)
{
    if (!make_output)
	return 0;

    ssize_t cols = xo_format_string_direct(xop, xbp, flags | XFF_UNESCAPE,
					   NULL, xp, len, -1,
					   need_enc, XF_ENC_UTF8);
    if (cols > 0) {
	if (XOF_ISSET(xop, XOF_COLUMNS))
	    xop->xo_columns += cols;
	if (XOIF_ISSET(xop, XOIF_ANCHOR))
	    xop->xo_anchor_columns += cols;
    }

    return cols;
}

/*
 * Parse a printf-style format specifier starting at 'cp' (which points
 * at the '%').  Fills in *xfp and returns a pointer to the conversion
 * character, or NULL on error.
 *
 * Note that 'n', 'v', and '$' are not supported.
 */
static const char *
xo_parse_format_spec (xo_handle_t *xop, xo_format_t *xfp,
		      const char *cp, const char *ep, const char *fmt)
{
    for (cp += 1; cp < ep; cp++) {
	if (*cp == 'l')
	    xfp->xf_lflag += 1;
	else if (*cp == 'h')
	    xfp->xf_hflag += 1;
	else if (*cp == 'j')
	    xfp->xf_jflag += 1;
	else if (*cp == 't')
	    xfp->xf_tflag += 1;
	else if (*cp == 'z')
	    xfp->xf_zflag += 1;
	else if (*cp == 'q')
	    xfp->xf_qflag += 1;
	else if (*cp == '.') {
	    if (xfp->xf_dots + 1 >= XF_WIDTH_NUM) {
		xo_failure(xop, "Too many dots in format: '%s'", fmt);
		return NULL;
	    }

	    xfp->xf_dots += 1;	/* Increment it (after check) */

	} else if (*cp == '-')
	    xfp->xf_seen_minus = 1;
	else if (isdigit((int) *cp)) {
	    if (xfp->xf_leading_zero < 0)
		xfp->xf_leading_zero = (*cp == '0');
	    xo_bump_width(xfp, *cp - '0');
	} else if (*cp == '*') {
	    xfp->xf_stars += 1;
	    xfp->xf_star[xfp->xf_dots] = 1;
	} else if (strchr("diouxXDOUeEfFgGaAcCsSpm", *cp) != NULL)
	    break;
	else if (*cp == 'n' || *cp == 'v') {
	    xo_failure(xop, "unsupported format: '%s'", fmt);
	    return NULL;
	}
    }

    if (cp == ep)
	xo_failure(xop, "field format missing format character: %s", fmt);

    xfp->xf_fc = *cp;
    return cp;
}

/*
 * Emit the value for one format specifier into xbp.  sp points to the
 * leading '%' of the specifier; cp points to the conversion character.
 * Returns 0 on success, -1 on error.
 */
static ssize_t
xo_emit_field_value (xo_handle_t *xop, xo_buffer_t *xbp,
		     xo_xff_flags_t flags, xo_format_t *xfp,
		     const char *sp, const char *cp, int style)
{
    ssize_t rc = 0;

    if (xfp->xf_skip)
	return 0;

    xo_buffer_t *fbp = &xop->xo_fmt;
    ssize_t len = cp - sp + 1;
    if (xo_check_for_room(xop, fbp, len + 1))
	return -1;

    char *newfmt = fbp->xb_curp;
    memcpy(newfmt, sp, len);
    newfmt[0] = '%';		/* If we skipped over a "%@...@s" format */
    newfmt[len] = '\0';

    /*
     * Bad news: our strings are UTF-8, but the stock printf
     * functions won't handle field widths for wide characters
     * correctly.  So we have to handle this ourselves.
     */
    if (xop->xo_formatter == NULL
	    && (xfp->xf_fc == 's' || xfp->xf_fc == 'S'
		|| xfp->xf_fc == 'm')) {

	xfp->xf_enc = (xfp->xf_fc == 'm') ? XF_ENC_UTF8
	    : (xfp->xf_lflag || (xfp->xf_fc == 'S')) ? XF_ENC_WIDE
	    : xfp->xf_hflag ? XF_ENC_LOCALE : XF_ENC_UTF8;

	rc = xo_format_string(xop, xbp, flags, xfp);

	if ((flags & XFF_TRIM_WS) && xo_style_is_encoding(xop))
	    rc = xo_trim_ws(xbp, rc);

    } else {
	ssize_t columns = rc = xo_vsnprintf(xop, xbp, newfmt, xop->xo_vap);

	if (rc > 0) {
	    /*
	     * For XML and HTML, we need "&<>" processing; for JSON,
	     * it's quotes.  Text gets nothing.
	     *
	     * Also we trim (the 't' modifier) for all styles _except_
	     * text and html.
	     */
	    switch (style) {
	    case XO_STYLE_XML:
		if (flags & XFF_TRIM_WS)
		    columns = rc = xo_trim_ws(xbp, rc);
		rc = xo_escape_xml(xop, xbp, rc, flags);
		break;

	    case XO_STYLE_HTML:
		rc = xo_escape_xml(xop, xbp, rc, (flags & XFF_ATTR));
		break;

	    case XO_STYLE_JSON:
		if (flags & XFF_TRIM_WS)
		    columns = rc = xo_trim_ws(xbp, rc);
		rc = xo_escape_json(xop, xbp, rc, flags);
		break;

	    case XO_STYLE_SDPARAMS:
		if (flags & XFF_TRIM_WS)
		    columns = rc = xo_trim_ws(xbp, rc);
		rc = xo_escape_sdparams(xop, xbp, rc, 0);
		break;

	    case XO_STYLE_ENCODER:
		if (flags & XFF_TRIM_WS)
		    columns = rc = xo_trim_ws(xbp, rc);
		break;
	    }

	    /*
	     * We can assume all the non-%s data we've
	     * added is ASCII, so the columns and bytes are the
	     * same.  xo_format_string handles all the fancy
	     * string conversions and updates xo_anchor_columns
	     * accordingly.
	     */
	    if (XOF_ISSET(xop, XOF_COLUMNS))
		xop->xo_columns += columns;
	    if (XOIF_ISSET(xop, XOIF_ANCHOR))
		xop->xo_anchor_columns += columns;
	}
    }

    if (rc > 0)
	xbp->xb_curp += rc;

    return 0;
}

/*
 * Advance xop->xo_vap past the argument consumed by one format specifier.
 */
static void
xo_advance_vap (xo_handle_t *xop, xo_format_t *xfp)
{
    if (XOF_ISSET(xop, XOF_NO_VA_ARG))
	return;

    if (xfp->xf_fc == 's' || xfp->xf_fc == 'S') {
	/*
	 * The 'S' and 's' formats are normally handled in
	 * xo_format_string, but if we skipped it, then we
	 * need to pop it.
	 */
	if (xfp->xf_skip)
	    va_arg(xop->xo_vap, char *);

    } else if (xfp->xf_fc == 'm') {
	/* Nothing on the stack for "%m" */

    } else {
	int s;
	for (s = 0; s < XF_WIDTH_NUM; s++) {
	    if (xfp->xf_star[s])
		va_arg(xop->xo_vap, int);
	}

	if (strchr("diouxXDOU", xfp->xf_fc) != NULL) {
	    if (xfp->xf_hflag > 1) {
		va_arg(xop->xo_vap, int);

	    } else if (xfp->xf_hflag > 0) {
		va_arg(xop->xo_vap, int);

	    } else if (xfp->xf_lflag > 1) {
		va_arg(xop->xo_vap, unsigned long long);

	    } else if (xfp->xf_lflag > 0) {
		va_arg(xop->xo_vap, unsigned long);

	    } else if (xfp->xf_jflag > 0) {
		va_arg(xop->xo_vap, intmax_t);

	    } else if (xfp->xf_tflag > 0) {
		va_arg(xop->xo_vap, ptrdiff_t);

	    } else if (xfp->xf_zflag > 0) {
		va_arg(xop->xo_vap, size_t);

	    } else if (xfp->xf_qflag > 0) {
		va_arg(xop->xo_vap, quad_t);

	    } else {
		va_arg(xop->xo_vap, int);
	    }
	} else if (strchr("eEfFgGaA", xfp->xf_fc) != NULL)
	    if (xfp->xf_lflag)
		xo_safe_va_arg_long_double(xop);
	    else
		va_arg(xop->xo_vap, double);

	else if (xfp->xf_fc == 'C' || (xfp->xf_fc == 'c' && xfp->xf_lflag))
	    va_arg(xop->xo_vap, wint_t);

	else if (xfp->xf_fc == 'c')
	    va_arg(xop->xo_vap, int);

	else if (xfp->xf_fc == 'p')
	    va_arg(xop->xo_vap, void *);
    }
}

/*
 * Interface to format a single field.  The arguments are in xo_vap,
 * and the format is in 'fmt'.  If 'xbp' is null, we use xop->xo_data;
 * this is the most common case.
 */
static ssize_t
xo_do_format_field (xo_handle_t *xop, xo_buffer_t *xbp,
		const char *fmt, ssize_t flen, xo_xff_flags_t flags)
{
    xo_format_t xf;
    const char *cp, *ep, *sp, *xp = NULL;
    int style = (flags & XFF_XML) ? XO_STYLE_XML : xo_style(xop);
    unsigned make_output = !(flags & XFF_NO_OUTPUT) ? 1 : 0;
    int need_enc = xo_needed_encoding(xop);
    int real_need_enc = need_enc;
    ssize_t old_cols = xop->xo_columns;

    /* The gettext interface is UTF-8, so we'll need that for now */
    if (flags & XFF_GT_FIELD)
	need_enc = XF_ENC_UTF8;

    if (xbp == NULL)
	xbp = &xop->xo_data;

    ssize_t start_offset = xo_buf_offset(xbp);

    for (cp = fmt, ep = fmt + flen; cp < ep; cp++) {
	/*
	 * Since we're starting a new field, save the starting offset.
	 * We'll need this later for field-related operations.
	 */

	if (*cp != '%') {
	add_one:
	    if (xp == NULL)
		xp = cp;

	    if (*cp == '\\' && cp[1] != '\0')
		cp += 1;
	    continue;

	} else if (cp + 1 < ep && cp[1] == '%') {
	    cp += 1;
	    goto add_one;
	}

	if (xp) {
	    if (xo_flush_literal(xop, xbp, flags, make_output, need_enc,
				 xp, cp - xp) < 0)
		return -1;

	    xp = NULL;
	}

	bzero(&xf, sizeof(xf));
	xf.xf_leading_zero = -1;
	xf.xf_width[0] = xf.xf_width[1] = xf.xf_width[2] = -1;

	/*
	 * "%@" starts an XO-specific set of flags:
	 *   @X@ - XML-only field; ignored if style isn't XML
	 */
	if (cp[1] == '@') {
	    for (cp += 2; cp < ep; cp++) {
		if (*cp == '@') {
		    break;
		}
		if (*cp == '*') {
		    /*
		     * '*' means there's a "%*.*s" value in vap that
		     * we want to ignore
		     */
		    if (!XOF_ISSET(xop, XOF_NO_VA_ARG))
			(void) va_arg(xop->xo_vap, int);
		}
	    }
	}

	/* Hidden fields are only visible to JSON and XML */
	if (XOF_ISSET(xop, XFF_ENCODE_ONLY)) {
	    if (style != XO_STYLE_XML
		    && !xo_style_is_encoding(xop))
		xf.xf_skip = 1;
	} else if (XOF_ISSET(xop, XFF_DISPLAY_ONLY)) {
	    if (style != XO_STYLE_TEXT
		    && xo_style(xop) != XO_STYLE_HTML)
		xf.xf_skip = 1;
	}

	if (!make_output)
	    xf.xf_skip = 1;

	/*
	 * Looking at one piece of a format; find the end and
	 * call snprintf.  Then advance xo_vap on our own.
	 */
	sp = cp;		/* Save start pointer */
	cp = xo_parse_format_spec(xop, &xf, cp, ep, fmt);
	if (cp == NULL)
	    return -1;

	if (!XOF_ISSET(xop, XOF_NO_VA_ARG)) {
	    if (xf.xf_fc == 's' || xf.xf_fc == 'S') {
		/* Handle "%*.*.*s" */
		int s;
		for (s = 0; s < XF_WIDTH_NUM; s++) {
		    if (xf.xf_star[s]) {
			xf.xf_width[s] = va_arg(xop->xo_vap, int);

			/* Normalize a negative width value */
			if (xf.xf_width[s] < 0) {
			    if (s == 0) {
				xf.xf_width[0] = -xf.xf_width[0];
				xf.xf_seen_minus = 1;
			    } else
				xf.xf_width[s] = -1; /* Ignore negative values */
			}
		    }
		}
	    }
	}

	/* If no max is given, it defaults to size */
	if (xf.xf_width[XF_WIDTH_MAX] < 0 && xf.xf_width[XF_WIDTH_SIZE] >= 0)
	    xf.xf_width[XF_WIDTH_MAX] = xf.xf_width[XF_WIDTH_SIZE];

	if (xf.xf_fc == 'D' || xf.xf_fc == 'O' || xf.xf_fc == 'U')
	    xf.xf_lflag = 1;

	if (xo_emit_field_value(xop, xbp, flags, &xf, sp, cp, style) < 0)
	    return -1;

	xo_advance_vap(xop, &xf);
    }

    if (xp) {
	if (xo_flush_literal(xop, xbp, flags, make_output, need_enc,
			     xp, cp - xp) < 0)
	    return -1;

	xp = NULL;
    }

    if (flags & XFF_GT_FLAGS) {
	/*
	 * Handle gettext()ing the field by looking up the value
	 * and then copying it in, while converting to locale, if
	 * needed.
	 */
	ssize_t new_cols = xo_format_gettext(xop, flags, start_offset,
					 old_cols, real_need_enc);

	if (XOF_ISSET(xop, XOF_COLUMNS))
	    xop->xo_columns += new_cols - old_cols;
	if (XOIF_ISSET(xop, XOIF_ANCHOR))
	    xop->xo_anchor_columns += new_cols - old_cols;
    }

    return 0;
}

/*
 * Remove any numeric precision/width format from the format string by
 * inserting the "%" after the [0-9]+, returning the substring.
 */
static char *
xo_fix_encoding (xo_handle_t *xop UNUSED, char *encoding)
{
    char *cp = encoding;

    if (cp[0] != '%' || !isdigit((int) cp[1]))
	return encoding;

    for (cp += 2; *cp; cp++) {
	if (!isdigit((int) *cp))
	    break;
    }

    *--cp = '%';		/* Back off and insert the '%' */

    return cp;
}

static void
xo_color_append_html (xo_handle_t *xop)
{
    /*
     * If the color buffer has content, we add it now.  It's already
     * prebuilt and ready, since we want to add it to every <div>.
     */
    if (!xo_buf_is_empty(&xop->xo_color_buf)) {
	xo_buffer_t *xbp = &xop->xo_color_buf;

	xo_data_append(xop, xbp->xb_bufp, xbp->xb_curp - xbp->xb_bufp);
    }
}

/*
 * A wrapper for humanize_number that autoscales, since the
 * HN_AUTOSCALE flag scales as needed based on the size of
 * the output buffer, not the size of the value.  I also
 * wish HN_DECIMAL was more imperative, without the <10
 * test.  But the boat only goes where we want when we hold
 * the rudder, so xo_humanize fixes part of the problem.
 */
static ssize_t
xo_humanize (char *buf, ssize_t len, uint64_t value, int flags)
{
    int scale = 0;

    if (value) {
	uint64_t left = value;

	if (flags & HN_DIVISOR_1000) {
	    for ( ; left; scale++)
		left /= 1000;
	} else {
	    for ( ; left; scale++)
		left /= 1024;
	}
	scale -= 1;
    }
    
    return xo_humanize_number(buf, len, value, "", scale, flags);
}

/*
 * This is an area where we can save information from the handle for
 * later restoration.  We need to know what data was rendered to know
 * what needs cleaned up.
 */
typedef struct xo_humanize_save_s {
    ssize_t xhs_offset;		/* Saved xo_offset */
    ssize_t xhs_columns;	/* Saved xo_columns */
    ssize_t xhs_anchor_columns; /* Saved xo_anchor_columns */
} xo_humanize_save_t;

/*
 * Format a "humanized" value for a numeric, meaning something nice
 * like "44M" instead of "44470272".  We autoscale, choosing the
 * most appropriate value for K/M/G/T/P/E based on the value given.
 */
static void
xo_format_humanize (xo_handle_t *xop, xo_buffer_t *xbp,
		    xo_humanize_save_t *savep, xo_xff_flags_t flags)
{
    if (XOF_ISSET(xop, XOF_NO_HUMANIZE))
	return;

    ssize_t end_offset = xbp->xb_curp - xbp->xb_bufp;
    if (end_offset == savep->xhs_offset) /* Huh? Nothing to render */
	return;

    /*
     * We have a string that's allegedly a number. We want to
     * humanize it, which means turning it back into a number
     * and calling xo_humanize_number on it.
     */
    uint64_t value;
    char *ep;

    xo_buf_append(xbp, "", 1); /* NUL-terminate it */

    value = strtoull(xbp->xb_bufp + savep->xhs_offset, &ep, 0);
    if (!(value == ULLONG_MAX && errno == ERANGE)
	&& (ep != xbp->xb_bufp + savep->xhs_offset)) {
	/*
	 * There are few values where humanize_number needs
	 * more bytes than the original value.  I've used
	 * 10 as a rectal number to cover those scenarios.
	 */
	if (!xo_check_for_room(xop, xbp, 10)) {
	    xo_buf_set_offset(xbp, savep->xhs_offset);

	    ssize_t rc;
	    ssize_t left = (xbp->xb_bufp + xbp->xb_size) - xbp->xb_curp;
	    int hn_flags = HN_NOSPACE; /* On by default */

	    if (flags & XFF_HN_SPACE)
		hn_flags &= ~HN_NOSPACE;

	    if (flags & XFF_HN_DECIMAL)
		hn_flags |= HN_DECIMAL;

	    if (flags & XFF_HN_1000)
		hn_flags |= HN_DIVISOR_1000;

	    rc = xo_humanize(xbp->xb_curp, left, value, hn_flags);
	    if (rc > 0) {
		xbp->xb_curp += rc;
		xop->xo_columns = savep->xhs_columns + rc;
		xop->xo_anchor_columns = savep->xhs_anchor_columns + rc;
	    }
	}
    }
}

/*
 * Convenience function that either append a fixed value (if one is
 * given) or formats a field using a format string.  If it's
 * encode_only, then we can't skip formatting the field, since it may
 * be pulling arguments off the stack.
 */
static inline void
xo_simple_field (xo_handle_t *xop, unsigned encode_only,
		      const char *value, ssize_t vlen,
		      const char *fmt, ssize_t flen, xo_xff_flags_t flags)
{
    if (encode_only)
	flags |= XFF_NO_OUTPUT;

    if (vlen == 0)
	xo_do_format_field(xop, NULL, fmt, flen, flags);
    else if (!encode_only)
	xo_data_append_content(xop, value, vlen, flags);
}

/*
 * Html mode: append a <div> to the output buffer contain a field
 * along with all the supporting information indicated by the flags.
 */
static void
xo_buf_append_div (xo_handle_t *xop, const char *class, xo_xff_flags_t flags,
		   const char *name, ssize_t nlen,
		   const char *value, ssize_t vlen,
		   const char *fmt, ssize_t flen,
		   const char *encoding, ssize_t elen)
{
    static char div_start[] = "<div class=\"";
    static char div_tag[] = "\" data-tag=\"";
    static char div_xpath[] = "\" data-xpath=\"";
    static char div_key[] = "\" data-key=\"key";
    static char div_end[] = "\">";
    static char div_close[] = "</div>";

    /* The encoding format defaults to the normal format */
    if (encoding == NULL && fmt != NULL) {
	char *enc  = alloca(flen + 1);
	memcpy(enc, fmt, flen);
	enc[flen] = '\0';
	encoding = xo_fix_encoding(xop, enc);
	elen = strlen(encoding);
    }

    /*
     * To build our XPath predicate, we need to save the va_list before
     * we format our data, and then restore it before we format the
     * xpath expression.
     * Display-only keys implies that we've got an encode-only key
     * elsewhere, so we don't use them from making predicates.
     */
    int need_predidate = 
	(name && (flags & XFF_KEY) && !(flags & XFF_DISPLAY_ONLY)
	 && XOF_ISSET(xop, XOF_XPATH)) ? 1 : 0;

    if (need_predidate) {
	va_list va_local;

	va_copy(va_local, xop->xo_vap);
	if (xop->xo_checkpointer)
	    xop->xo_checkpointer(xop, xop->xo_vap, 0);

	/*
	 * Build an XPath predicate expression to match this key.
	 * We use the format buffer.
	 */
	xo_buffer_t *pbp = &xop->xo_predicate;
	xo_buf_reset(pbp); /* Restart buffer */

	xo_buf_append(pbp, "[", 1);
	xo_buf_escape(xop, pbp, name, nlen, 0);
	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_buf_append(pbp, " = '", 4);
	else
	    xo_buf_append(pbp, "='", 2);

	xo_xff_flags_t pflags = flags | XFF_XML | XFF_ATTR;
	pflags &= ~(XFF_NO_OUTPUT | XFF_ENCODE_ONLY);
	xo_do_format_field(xop, pbp, encoding, elen, pflags);

	xo_buf_append(pbp, "']", 2);

	/* Now we record this predicate expression in the stack */
	xo_stack_t *xsp = xo_stack_cur(xop);
	ssize_t olen = xsp->xs_keys ? strlen(xsp->xs_keys) : 0;
	ssize_t dlen = pbp->xb_curp - pbp->xb_bufp;

	char *cp = xo_realloc(xsp->xs_keys, olen + dlen + 1);
	if (cp) {
	    memcpy(cp + olen, pbp->xb_bufp, dlen);
	    cp[olen + dlen] = '\0';
	    xsp->xs_keys = cp;
	}

	/* Now we reset the xo_vap as if we were never here */
	va_end(xop->xo_vap);
	va_copy(xop->xo_vap, va_local);
	va_end(va_local);
	if (xop->xo_checkpointer)
	    xop->xo_checkpointer(xop, xop->xo_vap, 1);
    }

    if (flags & XFF_ENCODE_ONLY) {
	/*
	 * Even if this is encode-only, we need to go through the
	 * work of formatting it to make sure the args are cleared
	 * from xo_vap.  This is not true when vlen is zero, since
	 * that means our "value" isn't on the stack.
	 */
	xo_simple_field(xop, TRUE, NULL, 0, encoding, elen, flags);
	return;
    }

    xo_line_ensure_open(xop, 0);

    if (XOF_ISSET(xop, XOF_PRETTY))
	xo_buf_indent(xop, xop->xo_indent_by);

    xo_data_append(xop, div_start, sizeof(div_start) - 1);
    xo_data_append(xop, class, strlen(class));

    /*
     * If the color buffer has content, we add it now.  It's already
     * prebuilt and ready, since we want to add it to every <div>.
     */
    if (!xo_buf_is_empty(&xop->xo_color_buf)) {
	xo_buffer_t *xbp = &xop->xo_color_buf;

	xo_data_append(xop, xbp->xb_bufp, xbp->xb_curp - xbp->xb_bufp);
    }

    if (name) {
	xo_data_append(xop, div_tag, sizeof(div_tag) - 1);
	xo_data_escape(xop, name, nlen);

	/*
	 * Save the offset at which we'd place units.  See xo_format_units.
	 */
	if (XOF_ISSET(xop, XOF_UNITS)) {
	    XOIF_SET(xop, XOIF_UNITS_PENDING);
	    /*
	     * Note: We need the '+1' here because we know we've not
	     * added the closing quote.  We add one, knowing the quote
	     * will be added shortly.
	     */
	    xop->xo_units_offset =
		xop->xo_data.xb_curp -xop->xo_data.xb_bufp + 1;
	}

	if (XOF_ISSET(xop, XOF_XPATH)) {
	    int i;
	    xo_stack_t *xsp;

	    xo_data_append(xop, div_xpath, sizeof(div_xpath) - 1);
	    if (xop->xo_leading_xpath)
		xo_data_append(xop, xop->xo_leading_xpath,
			       strlen(xop->xo_leading_xpath));

	    for (i = 0; i <= xop->xo_depth; i++) {
		xsp = &xop->xo_stack[i];
		if (xsp->xs_name == NULL)
		    continue;

		/*
		 * XSS_OPEN_LIST and XSS_OPEN_LEAF_LIST stack frames
		 * are directly under XSS_OPEN_INSTANCE frames so we
		 * don't need to put these in our XPath expressions.
		 */
		if (xsp->xs_state == XSS_OPEN_LIST
			|| xsp->xs_state == XSS_OPEN_LEAF_LIST)
		    continue;

		xo_data_append(xop, "/", 1);
		xo_data_escape(xop, xsp->xs_name, strlen(xsp->xs_name));
		if (xsp->xs_keys) {
		    /* Don't show keys for the key field */
		    if (i != xop->xo_depth || !(flags & XFF_KEY))
			xo_data_append(xop, xsp->xs_keys, strlen(xsp->xs_keys));
		}
	    }

	    xo_data_append(xop, "/", 1);
	    xo_data_escape(xop, name, nlen);
	}

	if (XOF_ISSET(xop, XOF_INFO) && xop->xo_info) {
	    static char in_type[] = "\" data-type=\"";
	    static char in_help[] = "\" data-help=\"";

	    xo_info_t *xip = xo_info_find(xop, name, nlen);
	    if (xip) {
		if (xip->xi_type) {
		    xo_data_append(xop, in_type, sizeof(in_type) - 1);
		    xo_data_escape(xop, xip->xi_type, strlen(xip->xi_type));
		}
		if (xip->xi_help) {
		    xo_data_append(xop, in_help, sizeof(in_help) - 1);
		    xo_data_escape(xop, xip->xi_help, strlen(xip->xi_help));
		}
	    }
	}

	if ((flags & XFF_KEY) && XOF_ISSET(xop, XOF_KEYS))
	    xo_data_append(xop, div_key, sizeof(div_key) - 1);
    }

    xo_buffer_t *xbp = &xop->xo_data;
    ssize_t base_offset = xbp->xb_curp - xbp->xb_bufp;

    xo_data_append(xop, div_end, sizeof(div_end) - 1);

    xo_humanize_save_t save;	/* Save values for humanizing logic */

    save.xhs_offset = xbp->xb_curp - xbp->xb_bufp;
    save.xhs_columns = xop->xo_columns;
    save.xhs_anchor_columns = xop->xo_anchor_columns;

    xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

    if (flags & XFF_HUMANIZE) {
	/*
	 * Unlike text style, we want to retain the original value and
	 * stuff it into the "data-number" attribute.
	 */
	static const char div_number[] = "\" data-number=\"";
	ssize_t div_len = sizeof(div_number) - 1;

	ssize_t end_offset = xbp->xb_curp - xbp->xb_bufp;
	ssize_t olen = end_offset - save.xhs_offset;

	char *cp = alloca(olen + 1);
	memcpy(cp, xbp->xb_bufp + save.xhs_offset, olen);
	cp[olen] = '\0';

	xo_format_humanize(xop, xbp, &save, flags);

	if (!xo_check_for_room(xop, xbp, div_len + olen)) {
	    ssize_t new_offset = xbp->xb_curp - xbp->xb_bufp;


	    /* Move the humanized string off to the left */
	    memmove(xbp->xb_bufp + base_offset + div_len + olen,
		    xbp->xb_bufp + base_offset, new_offset - base_offset);

	    /* Copy the data_number attribute name */
	    memcpy(xbp->xb_bufp + base_offset, div_number, div_len);

	    /* Copy the original long value */
	    memcpy(xbp->xb_bufp + base_offset + div_len, cp, olen);
	    xbp->xb_curp += div_len + olen;
	}
    }

    xo_data_append(xop, div_close, sizeof(div_close) - 1);

    if (XOF_ISSET(xop, XOF_PRETTY))
	xo_data_append(xop, "\n", 1);
}

static void
xo_format_text (xo_handle_t *xop, const char *str, ssize_t len)
{
    switch (xo_style(xop)) {
    case XO_STYLE_TEXT:
	xo_buf_append_locale(xop, &xop->xo_data, str, len);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, "text", 0, NULL, 0, str, len, NULL, 0, NULL, 0);
	break;
    }
}

static void
xo_format_title (xo_handle_t *xop, xo_field_info_t *xfip,
		 const char *value, ssize_t vlen)
{
    const char *fmt = xfip->xfi_format;
    ssize_t flen = xfip->xfi_flen;
    xo_xff_flags_t flags = xfip->xfi_flags;

    static char div_open[] = "<div class=\"title";
    static char div_middle[] = "\">";
    static char div_close[] = "</div>";

    if (flen == 0) {
	fmt = "%s";
	flen = 2;
    }

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
    case XO_STYLE_JSON:
    case XO_STYLE_SDPARAMS:
    case XO_STYLE_ENCODER:
	/*
	 * Even though we don't care about text, we need to do
	 * enough parsing work to skip over the right bits of xo_vap.
	 */
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	return;
    }

    xo_buffer_t *xbp = &xop->xo_data;
    ssize_t start = xbp->xb_curp - xbp->xb_bufp;
    ssize_t left = xbp->xb_size - start;
    ssize_t rc;

    if (xo_style(xop) == XO_STYLE_HTML) {
	xo_line_ensure_open(xop, 0);
	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_buf_indent(xop, xop->xo_indent_by);
	xo_buf_append(&xop->xo_data, div_open, sizeof(div_open) - 1);
	xo_color_append_html(xop);
	xo_buf_append(&xop->xo_data, div_middle, sizeof(div_middle) - 1);
    }

    start = xbp->xb_curp - xbp->xb_bufp; /* Reset start */
    if (vlen) {
	char *newfmt = alloca(flen + 1);
	memcpy(newfmt, fmt, flen);
	newfmt[flen] = '\0';

	/* If len is non-zero, the format string apply to the name */
	char *newstr = alloca(vlen + 1);
	memcpy(newstr, value, vlen);
	newstr[vlen] = '\0';

	if (newstr[vlen - 1] == 's') {
	    char *bp;

	    rc = snprintf(NULL, 0, newfmt, newstr);
	    if (rc > 0) {
		/*
		 * We have to do this the hard way, since we might need
		 * the columns.
		 */
		bp = alloca(rc + 1);
		rc = snprintf(bp, rc + 1, newfmt, newstr);

		xo_data_append_content(xop, bp, rc, flags);
	    }
	    goto move_along;

	} else {
	    rc = snprintf(xbp->xb_curp, left, newfmt, newstr);
	    if (rc >= left) {
		if (xo_check_for_room(xop, xbp, rc))
		    return;
		left = xbp->xb_size - (xbp->xb_curp - xbp->xb_bufp);
		rc = snprintf(xbp->xb_curp, left, newfmt, newstr);
	    }

	    if (rc > 0) {
		if (XOF_ISSET(xop, XOF_COLUMNS))
		    xop->xo_columns += rc;
		if (XOIF_ISSET(xop, XOIF_ANCHOR))
		    xop->xo_anchor_columns += rc;
	    }
	}

    } else {
	xo_do_format_field(xop, NULL, fmt, flen, flags);

	/* xo_do_format_field moved curp, so we need to reset it */
	rc = xbp->xb_curp - (xbp->xb_bufp + start);
	xo_buf_set_offset(xbp, start);
    }

    /* If we're styling HTML, then we need to escape it */
    if (xo_style(xop) == XO_STYLE_HTML) {
	rc = xo_escape_xml(xop, xbp, rc, 0);
    }

    if (rc > 0)
	xbp->xb_curp += rc;

 move_along:
    if (xo_style(xop) == XO_STYLE_HTML) {
	xo_data_append(xop, div_close, sizeof(div_close) - 1);
	if (XOF_ISSET(xop, XOF_PRETTY))
	    xo_data_append(xop, "\n", 1);
    }
}

/*
 * strspn() with a string length
 */
static ssize_t
xo_strnspn (const char *str, size_t len,  const char *accept)
{
    ssize_t i;
    const char *cp, *ep;

    for (i = 0, cp = str, ep = str + len; cp < ep && *cp != '\0'; i++, cp++) {
	if (strchr(accept, *cp) == NULL)
	    break;
    }

    return i;
}

/*
 * Decide if a format string should be considered "numeric",
 * in the sense that the number does not need to be quoted.
 * This means that it consists only of a single numeric field
 * with nothing exotic or "interesting".  This means that
 * static values are never considered numeric.
 */
static int
xo_format_is_numeric (const char *fmt, ssize_t flen)
{
    if (flen <= 0 || *fmt++ != '%') /* Must start with '%' */
	return FALSE;
    flen -= 1;

    /* Handle leading flags; don't want "#" since JSON can't handle hex */
    ssize_t spn = xo_strnspn(fmt, flen, "0123456789.*+ -");
    if (spn >= flen)
	return FALSE;

    fmt += spn;			/* Move along the input string */
    flen -= spn;

    /* Handle the length modifiers */
    spn = xo_strnspn(fmt, flen, "hljtqz");
    if (spn >= flen)
	return FALSE;

    fmt += spn;			/* Move along the input string */
    flen -= spn;

    if (flen != 1)		/* Should only be one character left */
	return FALSE;

    return (strchr("diouDOUeEfFgG", *fmt) == NULL) ? FALSE : TRUE;
}

/*
 * Update the stack flags using the object flags, allowing callers
 * to monkey with the stack flags without even knowing they exist.
 */
static void
xo_stack_set_flags (xo_handle_t *xop)
{
    if (XOF_ISSET(xop, XOF_NOT_FIRST)) {
	xo_stack_t *xsp = xo_stack_cur(xop);

	xsp->xs_flags |= XSF_NOT_FIRST;
	XOF_CLEAR(xop, XOF_NOT_FIRST);
    }
}

static void
xo_format_prep (xo_handle_t *xop, xo_xff_flags_t flags)
{
    if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST) {
	xo_data_append(xop, ",", 1);
	if (!(flags & XFF_LEAF_LIST) && XOF_ISSET(xop, XOF_PRETTY))
	    xo_data_append(xop, "\n", 1);
    } else
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
}

#if 0
/* Useful debugging function */
void
xo_arg (xo_handle_t *xop);
void
xo_arg (xo_handle_t *xop)
{
    xop = xo_default(xop);
    fprintf(stderr, "0x%x", va_arg(xop->xo_vap, unsigned));
}
#endif /* 0 */

/*
 * We want to allow mapping from one "name" to another replacement
 * name, like "df --libxo mapfile=df.map", so we can change the
 * vocabulary as needed.  This means maintaining an array of old and
 * new names, along with the memory for the strings themselves.  We
 * want this to be specific to the handle, so when the user requests a
 * map, we don't break other users of libxo.  We'll need two fields in
 * the handle, one for the array, and one for the string buffer.
 */
#ifdef LIBXO_NEED_MAP
static int
xo_map_find (xo_handle_t *xop, const char *name, size_t len)
{
    for (int i = 0; i < xop->xo_map_len; i += 2) {
	if (strncmp(xop->xo_map[i], name, len) == 0)
	    return i;
    }

    return -1;
}

/*
 * Is the path something we can find in $XO_MAPDIR?  The current test
 * is simple: lack of '/'.
 */
static int
xo_is_shareable_filename (const char *path)
{
    return strchr(path, '/') == NULL;
}

#endif /* LIBXO_NEED_MAP */

/*
 * Find the replacement string for a tag, or return the tag itself
 */
static inline const char *
xo_map_name (xo_handle_t *xop UNUSED, const char *name)
{
#ifdef LIBXO_NEED_MAP
    if (name == NULL)
	return NULL;

    size_t len = strlen(name);
    for (int i = 0; i < xop->xo_map_len; i += 2) {
	if (strncmp(xop->xo_map[i], name, len) == 0)
	    return xop->xo_map[i + 1];
    }
#endif /* LIBXO_NEED_MAP */

    return name;
}

/*
 * Add a mapping from one tag name to another.  Both "from" and "to" are
 * UTF-8 strings.
 */
int
xo_map_add (xo_handle_t *xop UNUSED, const char *from UNUSED,
	    size_t flen UNUSED, const char *to UNUSED, size_t tlen UNUSED)
{
#ifdef LIBXO_NEED_MAP
    xop = xo_default(xop);

    int val = xo_map_find(xop, from, flen);
    if (val >= 0) {
	/* We hit a "from" value that's already there; replace the "to" */
	char *newp = xo_buf_append_val(&xop->xo_map_data, to, tlen);
	if (newp == NULL)
	    return -1;

	/* NUL terminate the string */
	if (!xo_buf_append_val(&xop->xo_map_data, "", 1))
	    return -1;

	xop->xo_map[val + 1] = newp;

	return 0;
    }

    if (xop->xo_map_len >= xop->xo_map_size) {
	char **newp = xo_realloc(xop->xo_map, xop->xo_map_size + XO_MAP_INCR);
	if (newp == NULL)
	    return -1;
	xop->xo_map = newp;
	xop->xo_map_size += XO_MAP_INCR;
    }

    char *new_from = xo_buf_append_val(&xop->xo_map_data, from, flen);
    if (new_from == NULL)
	return -1;

    /* NUL terminate the new string */
    if (!xo_buf_append_val(&xop->xo_map_data, "", 1))
	return -1;

    char *new_to = xo_buf_append_val(&xop->xo_map_data, to, tlen);
    if (new_to == NULL)
	return -1;

    /* NUL terminate the new string */
    if (!xo_buf_append_val(&xop->xo_map_data, "", 1))
	return -1;

    val = xop->xo_map_len;	/* Use next slot */

    xop->xo_map[val] = new_from;
    xop->xo_map[val + 1] = new_to;

    xop->xo_map_len += 2;	/* Consume the slot */
#endif /* LIBXO_NEED_MAP */

    return 0;
}

static int
xo_map_option (xo_handle_t *xop, const char *opts)
{
    const char *cp, *np, *vp, *ep;
    size_t nlen, vlen;

    for (np = opts; *np; np = ep) {
	cp = strchr(np, '=');
	if (cp == NULL)
	    break;

	nlen = cp - np;

	vp = cp + 1;		/* Skip '=' */
	ep = strchr(vp, ':');
	vlen = ep ? (size_t) (ep - vp) : strlen(vp);

	if (xo_map_add(xop, np, nlen, vp, vlen))
	    return -1;

	if (ep == NULL)
	    break;
	ep += 1;		/* Skip ':' */
    }

    return 0;
}

/*
 * Add a file of tag maps, with the format:
 *    # example comment
 *    old-tag=new-tag
 *    ancient=new-hotness
 * The file should be UTF-8.
 */
int
xo_map_add_file (xo_handle_t *xop UNUSED, const char *fname UNUSED)
{
#ifdef LIBXO_NEED_MAP
    const char bom0 = 0xEF, bom1 = 0xBB, bom2 = 0xBF;

    char buf[BUFSIZ], *np, *cp, *ep, *vp;
    int first = TRUE;

    xop = xo_default(xop);

    FILE *fp = fopen(fname, "r");
    if (fp == NULL) {
	if (!xo_is_shareable_filename(fname))
	    return -1;

	static const char dir[] = XO_MAPDIR;
	size_t dlen = sizeof(dir) - 1;
	size_t flen = strlen(fname);
	char *new_path = alloca(dlen + 1 + flen + 1);
	memcpy(new_path, dir, dlen);
        new_path[dlen] = '/';
	memcpy(new_path + dlen + 1, fname, flen);
	new_path[dlen + 1 + flen] = '\0';

	fp = fopen(new_path, "r");
	if (fp == NULL)
	    return -1;
    }

    for (;;) {
	if (fgets(buf, sizeof(buf), fp) == NULL)
	    break;

	/*
	 * The UTF-8 file can start with a "BOM":
	 *    https://en.wikipedia.org/wiki/Byte_order_mark
	 * So if we see this at the start of the file, we need to skip
	 * over it.
	 */
	cp = buf;
	if (first && buf[0] == bom0 && buf[1] == bom1 && buf[2] == bom2)
	    cp += 3;
	first = FALSE;

	/* Skip to the start of the name (the "from") */
	np = cp + strspn(cp, " \t\r\n");
	if (*np == '#')
	    continue;

	/* Skip to the "=" */
	cp = np + strcspn(np, " \t\r\n=");
	if (cp == np)
	    continue;

	/* Find the start and the end of the value (the "to") */
	vp = cp + 1 + strspn(cp + 1, " \t\r\n=");
	ep = vp + strcspn(vp, " \t\r\n;");
	if (ep == vp)
	    continue;

	(void) xo_map_add(xop, np, cp - np, vp, ep - vp);
    }

    fclose(fp);
#endif /* LIBXO_NEED_MAP */

    return 0;
}

/*
 * Define the filter-related functions exposed by the API.  The filter
 * library is dynamically loaded and needs these functions to keep its
 * data in our handle.  If compiled without LIBXO_NEED_FILTERS, these
 * turn into NULL functions that will allow the filter related code to
 * be optimized out.
 */
void
xo_set_filter_data (xo_handle_t *xop UNUSED, struct xo_filter_s *xfp UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    xop = xo_default(xop);
    xop->xo_filters = xfp;
#endif /* LIBXO_NEED_FILTERS */
}

struct xo_filter_s *
xo_get_filter_data (xo_handle_t *xop UNUSED, int create UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    xop = xo_default(xop);
    if (xop->xo_filters == NULL && create)
	xop->xo_filters = xo_filter_create(xop);

    return xop->xo_filters;
#else /* LIBXO_NEED_FILTERS */
    return NULL;
#endif /* LIBXO_NEED_FILTERS */
}

/*
 * This one is just a convenience function for the code in this file
 */
static inline xo_filter_t *
xo_filters (xo_handle_t *xop UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    return xop->xo_filters;
#else /* LIBXO_NEED_FILTERS */
    return NULL;
#endif /* LIBXO_NEED_FILTERS */
}

const char *
xo_filt_status_name (xo_filter_status_t fstatus)
{
    return (fstatus == 0) ? "zero" :
        (fstatus == XO_STATUS_TRACK) ? "track" :
        (fstatus == XO_STATUS_FULL) ? "full" :
        (fstatus == XO_STATUS_PRED) ? "predicate" :
        (fstatus == XO_STATUS_DEAD) ? "dead" : "unknown";
}

int
xo_add_filter (xo_handle_t *xop UNUSED, const char *input UNUSED)
{
    int rc = -1;

#ifdef LIBXO_NEED_FILTERS
    xop = xo_default(xop);

    rc = xo_load_filter_lib(xop); /* Reports its own error */
    if (rc)
	return rc;

    XOF_SET(xop, XOF_FILTER); /* Activate filtering */

    /*
     * The XOIF_FILTERING flag means we are _actively_ filtering,
     * meaning the the flush routines should not be flushing data.
     * When we start wanting to make output, we can turn this flag
     * off.
     */
    XOIF_SET(xop, XOIF_FILTERING);

    rc = xo_filter_add_one(xop, input);
    if (rc)
	xo_warnx("libxo could not add the requested filter");

#else /* LIBXO_NEED_FILTERS */
    xo_warnx("libxo filtering is not enabled");
#endif /* LIBXO_NEED_FILTERS */

    return rc;
}

/*
 * Return TRUE when the current output position is permanently
 * filtered out.  An active filter has determined that no content
 * generated here can ever appear in the final output.  Callers can
 * use this to skip expensive computation before calling xo_emit:
 *
 *     xo_open_container("foo");
 *     if (!xo_discarding_output()) {
 *         ... expensive work ...
 *         xo_emit("{:field/...}", value);
 *     }
 *     xo_close_container("foo");
 *
 * Returns FALSE (proceed normally) when filtering is disabled, when
 * no filter is loaded, or when the status is anything other than DEAD
 * (including TRACK and PRED, which still need key and predicate
 * fields to resolve matches).
 */
int
xo_discarding_output_h (xo_handle_t *xop UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    xop = xo_default(xop);
    if (!XOIF_ISSET(xop, XOIF_FILTERING))
	return FALSE;
    return xo_stack_cur(xop)->xs_fstatus == XO_STATUS_DEAD;
#else /* LIBXO_NEED_FILTERS */
    return FALSE;
#endif /* LIBXO_NEED_FILTERS */
}

int
xo_discarding_output (void)
{
    return xo_discarding_output_h(NULL);
}

#if defined(LIBXO_NEED_FILTERS) && defined(LIBXO_DEBUG)
static void
xo_filt_dump_escape_contents (char *buf, int bufsiz, char *data)
{
    while (bufsiz > 0 && *data) {
	if (*data == ' ') {
	    *buf = *data;
	} else if (isspace((int) *data)) {
	    *buf = ' ';
	} else if (!isprint((int) *data)) {
	    *buf = ' ';
	} else {
	    *buf = *data;
	}

	bufsiz -= 1;
	buf += 1;
	data += 1;
    }
    *buf = '\0';
}

static void
xo_filt_dump_context (xo_handle_t *xop, xo_buffer_t *xbp,
		      xo_ssize_t off, int size)
{
    xo_ssize_t len = xo_buf_len(xbp);

    if (size == 0)
	size = 32;		/* Magic "enough" number */
    
    int pre_len = (off < size) ? off : (len < size) ? len : size;
    int pre_off = off - pre_len;
    int post_len = (off + size > len) ? len - off : size;

    char buf[BUFSIZ];
    char obuf[BUFSIZ];

    snprintf(buf, sizeof(buf), "[%*.*s][%*.*s]",
	   pre_len, pre_len, xo_buf_data(xbp, pre_off),
	   post_len, post_len, xo_buf_data(xbp, off));

    xo_filt_dump_escape_contents(obuf, sizeof(obuf), buf);
    xo_dbg(xop, "        contents: %s", obuf);
}

/* Debugging helper function (w/ fake forward) */
void
xo_filt_dump (xo_handle_t *xop, const char *tag);
void
xo_filt_dump (xo_handle_t *xop, const char *tag)
{
    xop = xo_default(xop);	/* NULL if called from lldb */

    if (!XO_HAS_DEBUG(xop))
	return;

    xo_dbg(xop, "xo_filt_dump: %s current depth %d", tag ?: "", xop->xo_depth);

    for (int depth = xop->xo_depth; depth >= 0; depth--) {
	xo_stack_t *xsp = &xop->xo_stack[depth];

	xo_dbg(xop, "    %d: '%s' state %u=%s, status %u=%s, flags %#x, "
	       "rb_off %d, key_off %d, rb_flags %#x, keys %p",
	       depth, xsp->xs_name ?: "", 
	       xsp->xs_state, xo_state_name(xsp->xs_state),
	       xsp->xs_fstatus, xo_filt_status_name(xsp->xs_fstatus),
	       xsp->xs_flags,
	       (int) xsp->xs_rb_off, (int) xsp->xs_key_off, xsp->xs_keys);

	if (xsp->xs_rb_off != XS_OFFSET_CLEAR)
	    xo_filt_dump_context(xop, &xop->xo_data, xsp->xs_rb_off, 0);
    }
}
#endif /* LIBXO_NEED_FILTERS && LIBXO_DEBUG */

#ifdef LIBXO_NEED_FILTERS
/*
 * Result block filled in by xo_filt_compact_range().
 */
typedef struct xo_compact_result_s {
    xo_off_t xcr_write_off;	/* next write offset, or XS_OFFSET_CLEAR */
    int xcr_last_clear;		/* last visited frame had xs_rb_off CLEAR */
    int xcr_prev_had_key;	/* last kept frame ended with a key field */
} xo_compact_result_t;

/*
 * Walk ancestor frames [first, end) — 'end' is exclusive — compacting
 * each one down to its opening tag plus any key fields.  Non-key
 * sibling content accumulated while the frame was TRACK is discarded
 * via memmove.  The JSON leading-comma invariant is handled here: if
 * a frame's tag begins with a 2-byte separator (",\n" or ", ") and
 * every prior child in that gap was also discarded (prev_keep_end <
 * tag_start and parent kept no key), the separator is stripped so the
 * compacted tag doesn't start with a stray comma.
 *
 * On return, rp is filled with the next write offset (xcr_write_off),
 * whether the last frame visited was already committed
 * (xcr_last_clear), and whether the last kept frame ended with a key
 * (xcr_prev_had_key).  Callers use these to relocate any trailing
 * content and strip a leading comma from it when needed.
 */
static void
xo_filt_compact_range (xo_handle_t *xop, xo_stack_t *first, xo_stack_t *end,
		       xo_filter_status_t fstatus, xo_compact_result_t *rp)
{
    xo_buffer_t *xbp = &xop->xo_data;
    xo_off_t prev_keep_end = XS_OFFSET_CLEAR;

    rp->xcr_write_off = XS_OFFSET_CLEAR;
    rp->xcr_last_clear = TRUE;
    rp->xcr_prev_had_key = FALSE;

    for (xo_stack_t *xsp = first; xsp < end; xsp++) {
	XO_DBG(xop, "xo_filt_compact_range: frame %u rb_off %d "
	       "tag_end %d key_off %d",
	       (unsigned)(xsp - xop->xo_stack),
	       (int) xsp->xs_rb_off, (int) xsp->xs_tag_end,
	       (int) xsp->xs_key_off);

	xo_off_t key_off = xsp->xs_key_off;
	xsp->xs_fstatus = fstatus;
	xsp->xs_key_off = XS_OFFSET_CLEAR;

	if (xsp->xs_rb_off == XS_OFFSET_CLEAR) {
	    prev_keep_end = XS_OFFSET_CLEAR;
	    rp->xcr_prev_had_key = FALSE;
	    rp->xcr_last_clear = TRUE;
	    continue;
	}
	rp->xcr_last_clear = FALSE;

	xo_off_t tag_start = xsp->xs_rb_off;
	xo_off_t keep_end = (xsp->xs_tag_end != XS_OFFSET_CLEAR)
	    ? xsp->xs_tag_end : tag_start;
	if (key_off != XS_OFFSET_CLEAR && key_off > keep_end)
	    keep_end = key_off;

	ssize_t keep_len = (keep_end > tag_start) ? (keep_end - tag_start) : 0;

	xo_off_t actual_tag_start = tag_start;
	if (xo_style(xop) == XO_STYLE_JSON
		&& (xsp->xs_rb_flags & XSF_NOT_FIRST)
		&& prev_keep_end != XS_OFFSET_CLEAR
		&& tag_start > prev_keep_end
		&& !rp->xcr_prev_had_key) {
	    actual_tag_start += 2;
	    keep_len = (keep_len > 2) ? keep_len - 2 : 0;
	}
	prev_keep_end = keep_end;
	rp->xcr_prev_had_key = (key_off != XS_OFFSET_CLEAR);

	if (rp->xcr_write_off == XS_OFFSET_CLEAR)
	    rp->xcr_write_off = actual_tag_start;
	if (keep_len > 0) {
	    if (actual_tag_start != rp->xcr_write_off)
		memmove(xbp->xb_bufp + rp->xcr_write_off,
			xbp->xb_bufp + actual_tag_start, keep_len);
	    rp->xcr_write_off += keep_len;
	}
	xsp->xs_rb_off = XS_OFFSET_CLEAR;
	xsp->xs_tag_end = XS_OFFSET_CLEAR;
    }
}

/*
 * Move 'len' bytes from 'start' in the output buffer to follow 'write_off',
 * returning the updated write position.  When write_off is CLEAR, the bytes
 * are already in place and only the end offset is returned.
 */
static xo_off_t
xo_filt_relocate (xo_buffer_t *xbp, xo_off_t write_off,
		  xo_off_t start, ssize_t len)
{
    if (len <= 0)
	return write_off;
    if (write_off == XS_OFFSET_CLEAR)
	write_off = start;
    if (start != write_off)
	memmove(xbp->xb_bufp + write_off, xbp->xb_bufp + start, len);
    return write_off + len;
}
#endif /* LIBXO_NEED_FILTERS */

/*
 * We want our parent objects (on the stack) to be emitted, so that
 * the filtered object has appropriate context.  We'll set their
 * fstatus and offsets so that they'll be emitted.  Also turn off
 * XOIF_FILTERING, so we know that we're not actively filtering.
 *
 * FYI: I'm using the "xo_filt_*" namespace for functions in this file
 * to keep filter-related functions "together", but distinct from the
 * _actual_ filtering code in xo_filter.[hc].
 */
static void
xo_filt_commit (xo_handle_t *xop UNUSED, xo_stack_t *cur UNUSED,
		      xo_filter_status_t fstatus UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    if (!XOF_ISSET(xop, XOF_FILTER))
	return;

    XO_DBG(xop, "xo_filt_commit: status -> %u=%s (stack depth %u)",
	   fstatus, xo_filt_status_name(fstatus), xop->xo_depth);
    xo_filt_dump(xop, "commit before");

    /*
     * If the current frame still holds tentative content (xs_rb_off set),
     * ancestor TRACK frames will have buffered non-tag content (e.g. sibling
     * fields before the matching path) that must be discarded.  Compact the
     * ancestors like xo_filt_commit_compact does, then move the current
     * frame's entire buffered range (opening tag plus any fields written so
     * far) to follow the compacted ancestors.  This handles the case where a
     * predicate field or key field resolves a PRED instance to FULL while
     * ancestor TRACK containers have accumulated junk in the buffer.
     */
    if (cur && cur->xs_rb_off != XS_OFFSET_CLEAR) {
	xo_buffer_t *xbp = &xop->xo_data;
	xo_off_t item_start = cur->xs_rb_off;
	xo_off_t item_end = xo_buf_offset(xbp);
	ssize_t item_len = (item_end > item_start) ? (item_end - item_start) : 0;

	xop->xo_stack[0].xs_fstatus = fstatus;

	/* Compact ancestors [1, cur); cur itself is relocated wholesale below */
	xo_compact_result_t r;
	xo_filt_compact_range(xop, xop->xo_stack + 1, cur, fstatus, &r);

	/* Commit cur, preserving its entire buffered content */
	cur->xs_fstatus = fstatus;
	cur->xs_rb_off = XS_OFFSET_CLEAR;
	cur->xs_tag_end = XS_OFFSET_CLEAR;
	cur->xs_key_off = XS_OFFSET_CLEAR;

	xo_off_t write_off =
	    xo_filt_relocate(xbp, r.xcr_write_off, item_start, item_len);
	if (write_off != XS_OFFSET_CLEAR)
	    xo_buf_set_offset(xbp, write_off);

	xo_filt_dump(xop, "commit after");
	XOIF_CLEAR(xop, XOIF_FILTERING);
	return;
    }

    for (xo_stack_t *xsp = xop->xo_stack; xsp <= cur; xsp++) {
	XO_DBG(xop, "xo_filt_commit: clearing offset %d, "
	       "status %u=%s -> %u=%s",
	       (int) xsp->xs_rb_off,
	       xsp->xs_fstatus, xo_filt_status_name(xsp->xs_fstatus),
	       fstatus, xo_filt_status_name(fstatus));

	xsp->xs_fstatus = fstatus;
	xsp->xs_rb_off = XS_OFFSET_CLEAR;
	xsp->xs_key_off = XS_OFFSET_CLEAR;
    }

    xo_filt_dump(xop, "commit after");

    XOIF_CLEAR(xop, XOIF_FILTERING);
#endif /* LIBXO_NEED_FILTERS */
}

/*
 * Commit a matching leaf field (e.g. a "cost[. > 55]" predicate that
 * resolves on the field itself).  Unlike xo_filt_commit, the matched unit
 * is a single leaf, not a pushed frame, so the enclosing container (cur) is
 * itself an ancestor whose buffered sibling fields must be discarded.  We
 * compact every open frame (including cur) down to its opening tag plus any
 * keys, then move the matched leaf [field_start..end] to follow.  Ancestor
 * tags become permanent (context), while trailing non-matching siblings are
 * left to the normal per-field skip/rollback path.
 */
static void
xo_filt_commit_field (xo_handle_t *xop UNUSED, xo_off_t field_start UNUSED,
		      xo_filter_status_t fstatus UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    if (!XOF_ISSET(xop, XOF_FILTER))
	return;

    xo_stack_t *cur = xo_stack_cur(xop);
    xo_buffer_t *xbp = &xop->xo_data;

    XO_DBG(xop, "xo_filt_commit_field: status -> %u=%s (depth %u, start %d)",
	   fstatus, xo_filt_status_name(fstatus), xop->xo_depth,
	   (int) field_start);
    xo_filt_dump(xop, "commit-field before");

    xo_off_t item_start = field_start;
    xo_off_t item_end = xo_buf_offset(xbp);
    ssize_t item_len = (item_end > item_start) ? (item_end - item_start) : 0;

    xop->xo_stack[0].xs_fstatus = fstatus;

    /* Compact every open frame, cur included, to tag + keys */
    xo_compact_result_t r;
    xo_filt_compact_range(xop, xop->xo_stack + 1, cur + 1, fstatus, &r);

    /*
     * In JSON the matched leaf carries a leading 2-byte separator (", " or
     * ",\n") since siblings preceded it.  Now that those siblings are gone and
     * the leaf becomes the first member after the parent's freshly-kept '{',
     * strip that separator.  Only do this when the parent was compacted here
     * (not already committed) and kept no key ahead of the leaf.
     */
    if (xo_style(xop) == XO_STYLE_JSON && !r.xcr_last_clear && !r.xcr_prev_had_key
	    && item_len >= 2 && xbp->xb_bufp[item_start] == ',') {
	item_start += 2;
	item_len -= 2;
    }

    /* Move the matched leaf to follow the compacted ancestor tags */
    xo_off_t write_off =
	xo_filt_relocate(xbp, r.xcr_write_off, item_start, item_len);
    if (write_off != XS_OFFSET_CLEAR)
	xo_buf_set_offset(xbp, write_off);

    xo_filt_dump(xop, "commit-field after");
    XOIF_CLEAR(xop, XOIF_FILTERING);
#endif /* LIBXO_NEED_FILTERS */
}


static void
xo_filt_handle_open_status (xo_handle_t *xop, xo_filter_status_t fstatus)
{
    xo_stack_t *xsp = xo_stack_cur(xop);
    
    if (fstatus == XO_STATUS_FULL) {
	xo_filt_commit(xop, xsp, fstatus);
	return;
    }

    if (fstatus == XO_STATUS_TRACK || fstatus == XO_STATUS_DEAD) {
	/*
	 * Predicate resolved FALSE; deactivate PRED bypass
	 * on instance frame
	 */
	if (xsp && xsp->xs_fstatus == XO_STATUS_PRED)
	    xsp->xs_fstatus = XO_STATUS_TRACK;
    }
}

/*
 * Compact-commit: emit ancestor opening tags and key fields, discarding
 * any non-key sibling content that accumulated while ancestors were TRACK.
 *
 * For each ancestor frame with a pending xs_rb_off, keep bytes from
 * xs_rb_off to max(xs_tag_end, xs_key_off): that span covers the opening
 * tag plus any key fields written before a deeper container opened.
 * Everything else (non-key sibling fields) is stripped.
 */
static void
xo_filt_commit_compact (xo_handle_t *xop UNUSED, xo_stack_t *cur UNUSED,
			xo_filter_status_t fstatus UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    if (!(xop->xo_flags & XOF_FILTER))
	return;

    XO_DBG(xop, "xo_filt_commit_compact: status -> %u=%s (stack depth %u)",
	   fstatus, xo_filt_status_name(fstatus), xop->xo_depth);

    xop->xo_stack[0].xs_fstatus = fstatus;
    int cur_was_pending = (cur->xs_rb_off != XS_OFFSET_CLEAR);

    xo_compact_result_t r;
    xo_filt_compact_range(xop, xop->xo_stack + 1, cur + 1, fstatus, &r);

    if (r.xcr_write_off != XS_OFFSET_CLEAR)
	xo_buf_set_offset(&xop->xo_data, r.xcr_write_off);

    /*
     * Reset NOT_FIRST/CONTENT on cur so the first real child (the
     * newly-FULL container about to open) does not inherit a stale JSON
     * comma.  Only do this when cur itself had pending (un-committed)
     * content: if cur's xs_rb_off was already CLEAR it was committed with
     * genuine content and NOT_FIRST must be preserved.
     * Also skip when cur kept key fields — those legitimately set NOT_FIRST.
     */
    if (cur_was_pending && !r.xcr_prev_had_key)
	cur->xs_flags &= ~XSF_RB_BITS;

    XOIF_CLEAR(xop, XOIF_FILTERING);
#endif /* LIBXO_NEED_FILTERS */
}

static void
xo_filt_handle_change_status (xo_handle_t *xop, xo_filter_status_t old_status,
			     xo_filter_status_t new_status)
{
    if (!XOF_ISSET(xop, XOF_FILTER))
	return;

    XO_DBG(xop, "xo_filt_handle_change_status: depth %d, old_status %u=%s, "
	   "new_status %u=%s", xop->xo_depth,
	   old_status, xo_filt_status_name(old_status),
	   new_status, xo_filt_status_name(new_status));

    if (new_status == XO_STATUS_FULL && old_status != XO_STATUS_FULL)
	xo_filt_commit_compact(xop, xo_stack_cur(xop), new_status);
}

static void
xo_filt_rollback (xo_handle_t *xop UNUSED, xo_stack_t *cur UNUSED,
		      xo_filter_status_t fstatus UNUSED,
		      xo_filter_status_t next_fstatus UNUSED)
{
#ifdef LIBXO_NEED_FILTERS
    if (!XOF_ISSET(xop, XOF_FILTER))
	return;

    XO_DBG(xop, "xo_filt_rollback: wiping %p (%d/depth %d) at %u, "
	   "status %u=%s",
	   cur, cur - xop->xo_stack, xop->xo_depth, cur->xs_rb_off,
	   fstatus, xo_filt_status_name(fstatus));
    xo_filt_dump(xop, "rollback before");

    /*
     * If the current status isn't FULL, we need to toss any output
     * We reset the current offset to the current stack, but only after
     * doing some sanity checking.
     */
    if (fstatus != XO_STATUS_FULL && cur->xs_rb_off != XS_OFFSET_CLEAR) {
	xo_buffer_t *xbp = &xop->xo_data;
	xo_off_t max_off = xo_buf_offset(xbp);
	xo_off_t cur_off = cur->xs_rb_off;

	if (cur_off < max_off) { /* Sanity check */
	    XO_DBG(xop, "xo_filt_rollback: rolling back to %u, depth %d",
		   cur_off, xop->xo_depth);
	    xo_buf_set_offset(xbp, cur_off);

	    if (cur_off == 0) {
		/* Going to zero means undo the "make output" flag */
		XOIF_CLEAR(xop, XOIF_MADE_OUTPUT);
		
		if (xo_style(xop) == XO_STYLE_JSON) {
		    /*
		     * If rolling back to the very start of the buffer,
		     * the JSON top-level '{' (if any) was inside the
		     * rolled-back range.  Clear TOP_EMITTED so xo_finish
		     * does not emit an unmatched '}'.
		     */
		    XO_DBG(xop, "xo_filt_rollback: clearing TOP_EMITTED");
		    XOIF_CLEAR(xop, XOIF_TOP_EMITTED);
		}
	    }

	    /*
	     * The JSON/XML open that pushed this frame may have set
	     * XSF_NOT_FIRST or XSF_CONTENT on our parent frame.
	     * Since we're discarding the child element, restore the
	     * parent flags to what they were before the open.
	     */
	    if (cur > xop->xo_stack) {
		xo_stack_t *parent = cur - 1;
		parent->xs_flags =
		    (parent->xs_flags & ~XSF_RB_BITS) | cur->xs_rb_flags;
	    }
	}
    }

    cur->xs_rb_off = XS_OFFSET_CLEAR;
    cur->xs_key_off = XS_OFFSET_CLEAR;

    xo_filt_dump(xop, "rollback after");

    if (next_fstatus != XO_STATUS_FULL)
	XOIF_SET(xop, XOIF_FILTERING);
#endif /* LIBXO_NEED_FILTERS */
}

static inline int
xo_filt_want_output (xo_handle_t *xop UNUSED, xo_filter_status_t fstatus)
{
#ifdef LIBXO_NEED_FILTERS
    switch (fstatus) {
    case XO_STATUS_ZERO:
    case XO_STATUS_FULL:
	return TRUE;
    default:
	return FALSE;
    }
#else /* LIBXO_NEED_FILTERS */
    return TRUE;
#endif /* LIBXO_NEED_FILTERS */

}

/*
 * Should we avoid flushing the output buffer?  The two reasons to
 * avoid this are:
 * - we have an anchor in place and will need to shift the contents
 * - we are filtering and may need to discard some of the buffered data
 */
static inline int
xo_avoid_flushing (xo_handle_t *xop)
{
    return XOIF_ISSET(xop, XOIF_ANCHOR | XOIF_FILTERING);
}

/*
 * Decide if the just-rendered field can be skipped (rolled back)
 */
static int
xo_filt_is_skippable (xo_handle_t *xop, xo_xff_flags_t flags,
		      const char *name, xo_ssize_t nlen,
		      xo_filter_status_t fstatus)
{
    /* If we're "full" open, don't skip anything */
    if (fstatus == XO_STATUS_FULL)
	return FALSE;

    /* Unless we're dea, we continue to care about key fields */
    if (flags & XFF_KEY)
	return (fstatus == XO_STATUS_DEAD);

    /*
     * In general, we don't want to pass "value" fields when only
     * tracking, but if we aren't dead and we need a field for a
     * predicate, we keep them all.
     */

    if (fstatus == XO_STATUS_DEAD) /* The dead don't care */
	return TRUE;

    /*
     * If we're inside a capture for a PRED instance, buffer all
     * sibling/nested content tentatively (XML/JSON only — encoder
     * state cannot be rolled back).  Only applies when the current
     * frame has an active whiteboard (xs_rb_off set); after compact
     * commit clears xs_rb_off, XOIF_FILTERING may still be set by
     * rollback of a sibling container, but fields at that level are
     * individually skippable and must not be suppressed here.
     */
    if (XOIF_ISSET(xop, XOIF_FILTERING) && xo_style(xop) != XO_STYLE_ENCODER
	    && xo_stack_cur(xop)->xs_rb_off != XS_OFFSET_CLEAR)
	return FALSE;

    if (fstatus == XO_STATUS_PRED)
	return FALSE;

    /* Don't skip if a pending predicate references this field */
    if (name) {
	int rc = xo_filter_needs_nonkey_field(xop, xo_filters(xop), name, nlen);
	XO_DBG(xop, "xo_filt_is_skippable: '%.*s' needs_nonkey_field -> %s",
	       nlen, name, rc ? "true" : "false");
	if (rc)
	    return FALSE;
    }

    return TRUE;
}

static int
xo_filt_skip (xo_handle_t *xop, xo_xff_flags_t flags,
	      const char *name, xo_ssize_t nlen)
{
    xo_filter_status_t fstatus = xo_filter_get_status(xop, xo_filters(xop));

    int rc = xo_filt_is_skippable(xop, flags, name, nlen, fstatus);

    XO_DBG(xop, "xo_filt_skip: '%.*s' %sdepth %d, status %u=%s -> %s",
	   nlen, name, (flags &XFF_KEY) ? "key " : "", xop->xo_depth,
	   fstatus, xo_filt_status_name(fstatus),
	   rc ? "true" : "false");

    return rc;
}

static xo_filter_status_t 
xo_filt_do_open_field (xo_handle_t *xop, const char *name, xo_ssize_t nlen,
		       const char *value, xo_ssize_t vlen, xo_off_t field_start,
		       int pass_field, xo_xff_flags_t flags)
{
    xo_filter_t *xfp = xo_filters(xop);
    xo_filter_status_t fstatus = xo_filter_get_status(xop, xfp);
    if (fstatus == XO_STATUS_DEAD)
	return fstatus;

    XO_DBG(xop, "xo_filt_do_open_field: %sdepth %d, status %u=%s",
	   (flags & XFF_KEY) ? "key " : "", xop->xo_depth,
	   fstatus, xo_filt_status_name(fstatus));

    if (flags & XFF_KEY) {
	fstatus = xo_filter_key(xop, xfp, name, nlen, value, vlen);
	xo_filt_handle_open_status(xop, fstatus);

	/* The caller doesn't want us calling open/close_field */
	if (!pass_field)
	    return fstatus;

    } else if ((fstatus == XO_STATUS_TRACK || fstatus == XO_STATUS_PRED)
	       && value && vlen > 0) {
	/*
	 * Non-key field: buffer its value if a pending predicate
	 * references it
	 */
	fstatus = xo_filter_pred_field(xop, xfp, name, nlen, value, vlen);
	xo_filt_handle_open_status(xop, fstatus);
    }

    fstatus = xo_filter_open_field(xop, xfp, name, nlen, value, vlen);
    if (fstatus == XO_STATUS_FULL) {
	/*
	 * A non-key field that matches on its own (e.g. a "cost[. > 55]"
	 * predicate resolving on the field itself) is a leaf match: the
	 * enclosing container is an ancestor whose buffered siblings must be
	 * dropped, so use the leaf-aware commit.  Key fields and styles that
	 * can't roll back (encoder) fall back to the frame-oriented commit.
	 */
	if (!(flags & XFF_KEY) && field_start != XS_OFFSET_CLEAR)
	    xo_filt_commit_field(xop, field_start, fstatus);
	else
	    xo_filt_commit(xop, xo_stack_cur(xop), fstatus);
    }

    return fstatus;
}

static xo_filter_status_t 
xo_filt_do_close_field (xo_handle_t *xop, const char *name, xo_ssize_t nlen,
			int pass_field, xo_xff_flags_t flags UNUSED)
{
    xo_filter_t *xfp = xo_filters(xop);
    xo_filter_status_t fstatus = xo_filter_get_status(xop, xfp);

    XO_DBG(xop, "xo_filt_do_close_field: depth %d, status %u=%s",
	   xop->xo_depth, fstatus, xo_filt_status_name(fstatus));

    /*
     * We need to decide whether to keep the field or not keep our
     * freshly-renderer field.
     */

    if ((flags & XFF_KEY) && !pass_field)
	return fstatus;

    fstatus = xo_filter_close_field(xop, xo_filters(xop), name, nlen);
    if (fstatus != XO_STATUS_FULL)
	XOIF_SET(xop, XOIF_FILTERING);

    return fstatus;
}

static void
xo_format_value_encoder (xo_handle_t *xop, const char *name, ssize_t nlen,
		 const char *value, ssize_t vlen,
		 const char *fmt, ssize_t flen,
		 const char *encoding, ssize_t elen, xo_xff_flags_t flags)
{
    if (flags & XFF_DISPLAY_ONLY) {
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	return;
    }

    int quote;
    if (flags & XFF_QUOTE)
	quote = 1;
    else if (flags & XFF_NOQUOTE)
	quote = 0;
    else if (flen == 0) {
	quote = 0;
	fmt = "true";	/* JSON encodes empty tags as a boolean true */
	flen = 4;
    } else if (strchr("diouxXDOUeEfFgGaAcCp", fmt[flen - 1]) == NULL)
	quote = 1;
    else
	quote = 0;

    if (encoding) {
	fmt = encoding;
	flen = elen;
    } else {
	char *enc  = alloca(flen + 1);
	memcpy(enc, fmt, flen);
	enc[flen] = '\0';
	fmt = xo_fix_encoding(xop, enc);
	flen = strlen(fmt);
    }

    if (nlen == 0) {
	static char missing[] = "missing-field-name";
	xo_failure(xop, "missing field name: %s", fmt);
	name = missing;
	nlen = sizeof(missing) - 1;
    }

    xo_data_append(xop, name, nlen);
    xo_data_append(xop, "", 1); /* NUL terminate the string */

    ssize_t value_offset = xo_buf_offset(&xop->xo_data);

    xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

    xo_data_append(xop, "", 1); /* NUL terminate the string */

    /* Find the formatted data in the buffer */
    const char *data = xo_buf_data(&xop->xo_data, value_offset);
    xo_ssize_t dlen = xo_buf_offset(&xop->xo_data) - value_offset - 1;

    /* Always call open and close, since they may change the status */
    if (XOF_ISSET(xop, XOF_FILTER))
	xo_filt_do_open_field(xop, name, nlen, data, dlen, XS_OFFSET_CLEAR,
			      FALSE, flags);


    if (!(XOF_ISSET(xop, XOF_FILTER) && xo_filt_skip(xop, flags, name, nlen))) {
	xo_encoder_handle(xop, quote ? XO_OP_STRING : XO_OP_CONTENT, NULL,
			  name, data, flags);
    }

    if (XOF_ISSET(xop, XOF_FILTER))
	xo_filt_do_close_field(xop, name, nlen, FALSE, flags);

    /* Reset our buffer, since we've sent the data to the encoder */
    xo_buf_reset(&xop->xo_data);
}

static void
xo_format_value_sdparams (xo_handle_t *xop, const char *name, ssize_t nlen,
		 const char *value, ssize_t vlen,
		 const char *fmt, ssize_t flen,
		 const char *encoding, ssize_t elen, xo_xff_flags_t flags)
{
    if (flags & XFF_DISPLAY_ONLY) {
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	return;
    }

    if (encoding) {
	fmt = encoding;
	flen = elen;
    } else {
	char *enc  = alloca(flen + 1);
	memcpy(enc, fmt, flen);
	enc[flen] = '\0';
	fmt = xo_fix_encoding(xop, enc);
	flen = strlen(fmt);
    }

    if (nlen == 0) {
	static char missing[] = "missing-field-name";
	xo_failure(xop, "missing field name: %s", fmt);
	name = missing;
	nlen = sizeof(missing) - 1;
    }

    xo_data_escape(xop, name, nlen);
    xo_data_append(xop, "=\"", 2);

    xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

    xo_data_append(xop, "\" ", 2);
}

static void
xo_format_value_json (xo_handle_t *xop, const char *name, ssize_t nlen,
		 const char *value, ssize_t vlen,
		 const char *fmt, ssize_t flen,
		 const char *encoding, ssize_t elen, xo_xff_flags_t flags,
		 xo_off_t *val_offp, xo_off_t *val_endp)
{
    if (flags & XFF_DISPLAY_ONLY) {
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	return;
    }

    if (encoding) {
	fmt = encoding;
	flen = elen;
    } else {
	char *enc  = alloca(flen + 1);
	memcpy(enc, fmt, flen);
	enc[flen] = '\0';
	fmt = xo_fix_encoding(xop, enc);
	flen = strlen(fmt);
    }

    xo_stack_set_flags(xop);

    int first = (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	? 0 : 1;

    xo_format_prep(xop, flags);

    int quote;
    if (flags & XFF_QUOTE)
	quote = 1;
    else if (flags & XFF_NOQUOTE)
	quote = 0;
    else if (vlen != 0)
	quote = 1;
    else if (flen == 0) {
	quote = 0;
	fmt = "true";	/* JSON encodes empty tags as a boolean true */
	flen = 4;
    } else if (xo_format_is_numeric(fmt, flen))
	quote = 0;
    else
	quote = 1;

    if (nlen == 0) {
	static char missing[] = "missing-field-name";
	xo_failure(xop, "missing field name: %s", fmt);
	name = missing;
	nlen = sizeof(missing) - 1;
    }

    xo_buffer_t *xbp = &xop->xo_data;
    int pretty = XOF_ISSET(xop, XOF_PRETTY);

    if (flags & XFF_LEAF_LIST) {
	if (!first && pretty)
	    xo_data_append(xop, "\n", 1);
	if (pretty)
	    xo_buf_indent(xop, -1);
    } else {
	if (pretty)
	    xo_buf_indent(xop, -1);
	xo_data_append(xop, "\"", 1);

	xbp = &xop->xo_data;
	ssize_t off = xbp->xb_curp - xbp->xb_bufp;

	xo_data_escape(xop, name, nlen);

	if (XOF_ISSET(xop, XOF_UNDERSCORES)) {
	    ssize_t coff = xbp->xb_curp - xbp->xb_bufp;
	    for ( ; off < coff; off++)
		if (xbp->xb_bufp[off] == '-')
		    xbp->xb_bufp[off] = '_';
	}
	xo_data_append(xop, "\":", 2);
	if (pretty)
	    xo_data_append(xop, " ", 1);
    }

    if (quote)
	xo_data_append(xop, "\"", 1);

    if (val_offp)
	*val_offp = xo_buf_offset(&xop->xo_data);

    xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

    if (val_endp)
	*val_endp = xo_buf_offset(&xop->xo_data);

    if (quote)
	xo_data_append(xop, "\"", 1);
}

static void
xo_format_value_xml (xo_handle_t *xop, const char *name, ssize_t nlen,
		     const char *value, ssize_t vlen,
		     const char *fmt, ssize_t flen,
		     const char *encoding, ssize_t elen, xo_xff_flags_t flags,
		     const char *leader)
{
    /*
     * Even though we're not making output, we still need to
     * let the formatting code handle the va_arg popping.
     */
    if (flags & XFF_DISPLAY_ONLY) {
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	return;
    }

    if (encoding) {
	fmt = encoding;
	flen = elen;
    } else {
	char *enc  = alloca(flen + 1);
	memcpy(enc, fmt, flen);
	enc[flen] = '\0';
	fmt = xo_fix_encoding(xop, enc);
	flen = strlen(fmt);
    }

    if (nlen == 0) {
	static char missing[] = "missing-field-name";
	xo_failure(xop, "missing field name: %s", fmt);
	name = missing;
	nlen = sizeof(missing) - 1;
    }

    ssize_t start_offset = xo_buf_offset(&xop->xo_data);

    int pretty = XOF_ISSET(xop, XOF_PRETTY);
    if (pretty)
	xo_buf_indent(xop, -1);

    xo_data_append(xop, "<", 1);
    if (*leader)
	xo_data_append(xop, leader, 1);
    xo_data_escape(xop, name, nlen);

    if (xop->xo_attrs.xb_curp != xop->xo_attrs.xb_bufp) {
	xo_data_append(xop, xop->xo_attrs.xb_bufp,
		       xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp);
	xo_buf_reset(&xop->xo_attrs);
    }

    /*
     * We indicate 'key' fields using the 'key' attribute.  While
     * this is really committing the crime of mixing meta-data with
     * data, it's often useful.  Especially when format meta-data is
     * difficult to come by.
     */
    if ((flags & XFF_KEY) && XOF_ISSET(xop, XOF_KEYS)) {
	static char attr[] = " key=\"key\"";
	xo_data_append(xop, attr, sizeof(attr) - 1);
    }

    /*
     * Save the offset at which we'd place units.  See xo_format_units.
     */
    if (XOF_ISSET(xop, XOF_UNITS)) {
	XOIF_SET(xop, XOIF_UNITS_PENDING);
	xop->xo_units_offset = xop->xo_data.xb_curp - xop->xo_data.xb_bufp;
    }

    xo_data_append(xop, ">", 1);

    ssize_t data_offset = xo_buf_offset(&xop->xo_data);

    xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

    const char *data = xo_buf_data(&xop->xo_data, data_offset);
    xo_ssize_t dlen = xo_buf_offset(&xop->xo_data) - data_offset;

    /* Always call open and close, since they may change the status */
    xo_filter_status_t fstatus UNUSED;
    if (XOF_ISSET(xop, XOF_FILTER)) {
	fstatus = xo_filt_do_open_field(xop, name, nlen, data, dlen,
					start_offset, TRUE, flags);
    }

    /*
     * We had to call xo_simple_field to format the data and
     * clear any elements of xo_varg.  But we can skip the rest of
     * the output (the close tag).
     */
    if (XOF_ISSET(xop, XOF_FILTER) && xo_filt_skip(xop, flags, name, nlen)) {
	/*
	 * Reset the current offset back to the saved one.
	 */
	xo_buf_set_offset(&xop->xo_data, start_offset);

    } else {
	/* We can't skip it, so we go ahead and make the closing tag */
	xo_data_append(xop, "</", 2);
	if (*leader)
	    xo_data_append(xop, leader, 1);
	xo_data_escape(xop, name, nlen);
	xo_data_append(xop, ">", 1);

	if (pretty)
	    xo_data_append(xop, "\n", 1);

	/* Record the "end of key" offset */
	if (flags & XFF_KEY) {
	    xo_stack_t *xsp = xo_stack_cur(xop);
	    xsp->xs_key_off = xo_buf_offset(&xop->xo_data);
	}
    }

    if (XOF_ISSET(xop, XOF_FILTER))
	xo_filt_do_close_field(xop, name, nlen, TRUE, flags);
}

static void
xo_format_value (xo_handle_t *xop, const char *name, ssize_t nlen,
		 const char *value, ssize_t vlen,
		 const char *fmt, ssize_t flen,
		 const char *encoding, ssize_t elen, xo_xff_flags_t flags)
{
    /* Passing NULL to memcpy is undefined behavior, so make a fake here */
    const char *rname = name ?: "";
    if (nlen < 0)
	nlen = 0;

    /*
     * Before we emit a value, we need to know that the frame is ready.
     */
    xo_stack_t *xsp = xo_stack_cur(xop);

    if (flags & XFF_LEAF_LIST) {
	/*
	 * Check if we've already started to emit normal leafs
	 * or if we're not in a leaf list.
	 */
	if ((xsp->xs_flags & (XSF_EMIT | XSF_EMIT_KEY))
	    || !(xsp->xs_flags & XSF_EMIT_LEAF_LIST)) {
	    char nbuf[nlen + 1];
	    memcpy(nbuf, rname, nlen);
	    nbuf[nlen] = '\0';

	    ssize_t rc = xo_transition(xop, 0, nbuf, XSS_EMIT_LEAF_LIST);
	    if (rc < 0)
		flags |= XFF_DISPLAY_ONLY | XFF_ENCODE_ONLY;
	    else
		xop->xo_stack[xop->xo_depth].xs_flags |= XSF_EMIT_LEAF_LIST;
	}

	xsp = xo_stack_cur(xop);
	if (xsp->xs_name) {
	    name = xsp->xs_name;
	    nlen = strlen(name);
	}

    } else if (flags & XFF_KEY) {
	/* Emitting a 'k' (key) field */
	if ((xsp->xs_flags & XSF_EMIT) && !(flags & XFF_DISPLAY_ONLY)) {
	    xo_failure(xop,
		       "key field emitted after normal value field: '%.*s'",
		       nlen, name);

	} else if (!(xsp->xs_flags & XSF_EMIT_KEY)) {
	    char nbuf[nlen + 1];
	    memcpy(nbuf, rname, nlen);
	    nbuf[nlen] = '\0';

	    ssize_t rc = xo_transition(xop, 0, nbuf, XSS_EMIT);
	    if (rc < 0)
		flags |= XFF_DISPLAY_ONLY | XFF_ENCODE_ONLY;
	    else
		xop->xo_stack[xop->xo_depth].xs_flags |= XSF_EMIT_KEY;

	    xsp = xo_stack_cur(xop);
	    xsp->xs_flags |= XSF_EMIT_KEY;
	}

    } else {
	/* Emitting a normal value field */
	if ((xsp->xs_flags & XSF_EMIT_LEAF_LIST)
	    || !(xsp->xs_flags & XSF_EMIT)) {
	    char nbuf[nlen + 1];
	    memcpy(nbuf, rname, nlen);
	    nbuf[nlen] = '\0';

	    ssize_t rc = xo_transition(xop, 0, nbuf, XSS_EMIT);
	    if (rc < 0)
		flags |= XFF_DISPLAY_ONLY | XFF_ENCODE_ONLY;
	    else
		xop->xo_stack[xop->xo_depth].xs_flags |= XSF_EMIT;

	    xsp = xo_stack_cur(xop);
	    xsp->xs_flags |= XSF_EMIT;
	}
    }

    xo_buffer_t *xbp = &xop->xo_data;
    xo_humanize_save_t save;	/* Save values for humanizing logic */

    if (name) {
	/*
	 * We have a name, but need to see if it's been remapped
	 * to a different name.  To look up the tag name, we need
	 * to make a local copy and NUL terminate it.
	 */
	char *new_name  = alloca(nlen + 1);
	memcpy(new_name, name, nlen);
	new_name[nlen] = '\0';

	name = xo_map_name(xop, new_name);
	nlen = strlen(name);	/* Need new length for new name */
    }

    const char *leader = xo_xml_leader_len(xop, name, nlen);

    switch (xo_style(xop)) {
    case XO_STYLE_TEXT:
	if (flags & XFF_ENCODE_ONLY)
	    flags |= XFF_NO_OUTPUT;

	save.xhs_offset = xbp->xb_curp - xbp->xb_bufp;
	save.xhs_columns = xop->xo_columns;
	save.xhs_anchor_columns = xop->xo_anchor_columns;

	xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);

	if (flags & XFF_HUMANIZE)
	    xo_format_humanize(xop, xbp, &save, flags);
	break;

    case XO_STYLE_HTML:
	if (flags & XFF_ENCODE_ONLY)
	    flags |= XFF_NO_OUTPUT;

	xo_buf_append_div(xop, "data", flags, name, nlen, value, vlen,
			  fmt, flen, encoding, elen);
	break;

    case XO_STYLE_XML:
	xo_format_value_xml(xop, name, nlen, value, vlen,
			    fmt, flen, encoding, elen, flags, leader);
	break;

    case XO_STYLE_JSON:
	if (XOF_ISSET(xop, XOF_FILTER)) {
	    xo_off_t json_start = xo_buf_offset(&xop->xo_data);
	    xo_xsf_flags_t saved_not_first =
		xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST;

	    /*
	     * Render first so we have the actual formatted value for key
	     * predicate evaluation.  val_off/val_end bracket only the rendered
	     * value bytes, excluding the surrounding JSON quotes.  Both are
	     * initialized to json_start so that an XFF_DISPLAY_ONLY early
	     * return leaves val_len == 0.  The filter uses vlen throughout
	     * and copies into its own allocation (xo_tframe_key_add), so
	     * passing the data buffer pointer directly is safe.
	     */
	    xo_off_t val_off = json_start, val_end = json_start;
	    xo_format_value_json(xop, name, nlen, value, vlen,
				 fmt, flen, encoding, elen, flags,
				 &val_off, &val_end);
	    xo_off_t val_len = val_end - val_off;

	    xo_filt_do_open_field(xop, name, nlen,
				  xo_buf_data(&xop->xo_data, val_off), val_len,
				  json_start, TRUE, flags);
	    if (xo_filt_skip(xop, flags, name, nlen)) {
		xo_buf_set_offset(&xop->xo_data, json_start);
		xop->xo_stack[xop->xo_depth].xs_flags =
		    (xop->xo_stack[xop->xo_depth].xs_flags & ~XSF_NOT_FIRST)
		    | saved_not_first;
	    } else if (flags & XFF_KEY) {
		/* Record end-of-key offset for compact-commit, same as XML path */
		xsp->xs_key_off = xo_buf_offset(&xop->xo_data);
	    }
	    xo_filt_do_close_field(xop, name, nlen, TRUE, flags);
	} else {
	    xo_format_value_json(xop, name, nlen, value, vlen,
				 fmt, flen, encoding, elen, flags, NULL, NULL);
	}
	break;

    case XO_STYLE_SDPARAMS:
	xo_format_value_sdparams(xop, name, nlen, value, vlen,
				 fmt, flen, encoding, elen, flags);
	break;

    case XO_STYLE_ENCODER:
	xo_format_value_encoder(xop, name, nlen, value, vlen,
				fmt, flen, encoding, elen, flags);
	break;
    }
}

static void
xo_set_gettext_domain (xo_handle_t *xop, xo_field_info_t *xfip,
		       const char *str, ssize_t len)
{
    const char *fmt = xfip->xfi_format;
    ssize_t flen = xfip->xfi_flen;

    /* Start by discarding previous domain */
    if (xop->xo_gt_domain) {
	xo_free(xop->xo_gt_domain);
	xop->xo_gt_domain = NULL;
    }

    /* An empty {G:} means no domainname */
    if (len == 0 && flen == 0)
	return;

    ssize_t start_offset = -1;
    if (len == 0 && flen != 0) {
	/* Need to do format the data to get the domainname from args */
	start_offset = xop->xo_data.xb_curp - xop->xo_data.xb_bufp;
	xo_do_format_field(xop, NULL, fmt, flen, 0);

	ssize_t end_offset = xop->xo_data.xb_curp - xop->xo_data.xb_bufp;
	len = end_offset - start_offset;
	str = xop->xo_data.xb_bufp + start_offset;
    }

    xop->xo_gt_domain = xo_strndup(str, len);

    /* Reset the current buffer point to avoid emitting the name as output */
    if (start_offset >= 0)
	xo_buf_set_offset(&xop->xo_data, start_offset);
}

static void
xo_format_content (xo_handle_t *xop, const char *class_name,
		   const char *tag_name,
		   const char *value, ssize_t vlen,
		   const char *fmt, ssize_t flen,
		   xo_xff_flags_t flags)
{
    switch (xo_style(xop)) {
    case XO_STYLE_TEXT:
	xo_simple_field(xop, FALSE, value, vlen, fmt, flen, flags);
	break;

    case XO_STYLE_HTML:
	xo_buf_append_div(xop, class_name, flags, NULL, 0,
			  value, vlen, fmt, flen, NULL, 0);
	break;

    case XO_STYLE_XML:
    case XO_STYLE_JSON:
    case XO_STYLE_SDPARAMS:
	if (tag_name) {
	    xo_open_container_h(xop, tag_name);
	    xo_format_value(xop, "message", 7, value, vlen,
			    fmt, flen, NULL, 0, flags);
	    xo_close_container_h(xop, tag_name);

	} else {
	    /*
	     * Even though we don't care about labels, we need to do
	     * enough parsing work to skip over the right bits of xo_vap.
	     */
	    xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	}
	break;

    case XO_STYLE_ENCODER:
	xo_simple_field(xop, TRUE, value, vlen, fmt, flen, flags);
	break;
    }
}

static const char *xo_color_names[] = {
    "default",	/* XO_COL_DEFAULT */
    "black",	/* XO_COL_BLACK */
    "red",	/* XO_CLOR_RED */
    "green",	/* XO_COL_GREEN */
    "yellow",	/* XO_COL_YELLOW */
    "blue",	/* XO_COL_BLUE */
    "magenta",	/* XO_COL_MAGENTA */
    "cyan",	/* XO_COL_CYAN */
    "white",	/* XO_COL_WHITE */
    NULL
};

static int
xo_color_find (const char *str)
{
    int i;

    for (i = 0; xo_color_names[i]; i++) {
	if (xo_streq(xo_color_names[i], str))
	    return i;
    }

    return -1;
}

static const char *xo_effect_names[] = {
    "reset",			/* XO_EFF_RESET */
    "normal",			/* XO_EFF_NORMAL */
    "bold",			/* XO_EFF_BOLD */
    "underline",		/* XO_EFF_UNDERLINE */
    "inverse",			/* XO_EFF_INVERSE */
    NULL
};

static const char *xo_effect_on_codes[] = {
    "0",			/* XO_EFF_RESET */
    "0",			/* XO_EFF_NORMAL */
    "1",			/* XO_EFF_BOLD */
    "4",			/* XO_EFF_UNDERLINE */
    "7",			/* XO_EFF_INVERSE */
    NULL
};

#if 0
/*
 * See comment below re: joy of terminal standards.  These can
 * be use by just adding:
 * +	if (newp->xoc_effects & bit)
 *	    code = xo_effect_on_codes[i];
 * +	else
 * +	    code = xo_effect_off_codes[i];
 * in xo_color_handle_text.
 */
static const char *xo_effect_off_codes[] = {
    "0",			/* XO_EFF_RESET */
    "0",			/* XO_EFF_NORMAL */
    "21",			/* XO_EFF_BOLD */
    "24",			/* XO_EFF_UNDERLINE */
    "27",			/* XO_EFF_INVERSE */
    NULL
};
#endif /* 0 */

static int
xo_effect_find (const char *str)
{
    int i;

    for (i = 0; xo_effect_names[i]; i++) {
	if (xo_streq(xo_effect_names[i], str))
	    return i;
    }

    return -1;
}

static void
xo_colors_parse (xo_handle_t *xop, xo_colors_t *xocp, char *str)
{
    if (xo_text_only())
	return;

    char *cp, *ep, *np, *xp;
    ssize_t len = strlen(str);
    int rc;

    /*
     * Possible tokens: colors, bg-colors, effects, no-effects, "reset".
     */
    for (cp = str, ep = cp + len - 1; cp && cp < ep; cp = np) {
	/* Trim leading whitespace */
	while (isspace((int) *cp))
	    cp += 1;

	np = strchr(cp, ',');
	if (np)
	    *np++ = '\0';

	/* Trim trailing whitespace */
	xp = cp + strlen(cp) - 1;
	while (isspace(*xp) && xp > cp)
	    *xp-- = '\0';

	if (cp[0] == 'f' && cp[1] == 'g' && cp[2] == '-') {
	    rc = xo_color_find(cp + 3);
	    if (rc < 0)
		goto unknown;

	    xocp->xoc_col_fg = rc;

	} else if (cp[0] == 'b' && cp[1] == 'g' && cp[2] == '-') {
	    rc = xo_color_find(cp + 3);
	    if (rc < 0)
		goto unknown;
	    xocp->xoc_col_bg = rc;

	} else if (cp[0] == 'n' && cp[1] == 'o' && cp[2] == '-') {
	    rc = xo_effect_find(cp + 3);
	    if (rc < 0)
		goto unknown;
	    xocp->xoc_effects &= ~(1 << rc);

	} else {
	    rc = xo_effect_find(cp);
	    if (rc < 0)
		goto unknown;
	    xocp->xoc_effects |= 1 << rc;

	    switch (1 << rc) {
	    case XO_EFF_RESET:
		xocp->xoc_col_fg = xocp->xoc_col_bg = 0;
		/* Note: not "|=" since we want to wipe out the old value */
		xocp->xoc_effects = XO_EFF_RESET;
		break;

	    case XO_EFF_NORMAL:
		xocp->xoc_effects &= ~(XO_EFF_BOLD | XO_EFF_UNDERLINE
				      | XO_EFF_INVERSE | XO_EFF_NORMAL);
		break;
	    }
	}
	continue;

    unknown:
	if (XOF_ISSET(xop, XOF_WARN))
	    xo_failure(xop, "unknown color/effect string detected: '%s'", cp);
    }
}

static inline int
xo_colors_enabled (xo_handle_t *xop UNUSED)
{
#ifdef LIBXO_TEXT_ONLY
    return 0;
#else /* LIBXO_TEXT_ONLY */
    return XOF_ISSET(xop, XOF_COLOR);
#endif /* LIBXO_TEXT_ONLY */
}

/*
 * If the color map is in use (--libxo colors=xxxx), then update
 * the incoming foreground and background colors from the map.
 */
static void
xo_colors_update (xo_handle_t *xop UNUSED, xo_colors_t *newp UNUSED)
{
#ifndef LIBXO_TEXT_ONLY
    xo_color_t fg = newp->xoc_col_fg;
    if (XOF_ISSET(xop, XOF_COLOR_MAP) && fg < XO_NUM_COLORS)
	fg = xop->xo_color_map_fg[fg]; /* Fetch from color map */
    newp->xoc_col_fg = fg;

    xo_color_t bg = newp->xoc_col_bg;
    if (XOF_ISSET(xop, XOF_COLOR_MAP) && bg < XO_NUM_COLORS)
	bg = xop->xo_color_map_bg[bg]; /* Fetch from color map */
    newp->xoc_col_bg = bg;
#endif /* LIBXO_TEXT_ONLY */
}

static void
xo_colors_handle_text (xo_handle_t *xop, xo_colors_t *newp)
{
    char buf[BUFSIZ];
    char *cp = buf, *ep = buf + sizeof(buf);
    unsigned i, bit;
    xo_colors_t *oldp = &xop->xo_colors;
    const char *code = NULL;

    /*
     * Start the buffer with an escape.  We don't want to add the '['
     * now, since we let xo_effect_text_add unconditionally add the ';'.
     * We'll replace the first ';' with a '[' when we're done.
     */
    *cp++ = 0x1b;		/* Escape */

    /*
     * Terminals were designed back in the age before "certainty" was
     * invented, when standards were more what you'd call "guidelines"
     * than actual rules.  Anyway we can't depend on them to operate
     * correctly.  So when display attributes are changed, we punt,
     * reseting them all and turning back on the ones we want to keep.
     * Longer, but should be completely reliable.  Savvy?
     */
    if (oldp->xoc_effects != (newp->xoc_effects & oldp->xoc_effects)) {
	newp->xoc_effects |= XO_EFF_RESET;
	oldp->xoc_effects = 0;
    }

    for (i = 0, bit = 1; xo_effect_names[i]; i++, bit <<= 1) {
	if ((newp->xoc_effects & bit) == (oldp->xoc_effects & bit))
	    continue;

	code = xo_effect_on_codes[i];

	cp += snprintf(cp, ep - cp, ";%s", code);
	if (cp >= ep)
	    return;		/* Should not occur */

	if (bit == XO_EFF_RESET) {
	    /* Mark up the old value so we can detect current values as new */
	    oldp->xoc_effects = 0;
	    oldp->xoc_col_fg = oldp->xoc_col_bg = XO_COL_DEFAULT;
	}
    }

    xo_color_t fg = newp->xoc_col_fg;
    if (fg != oldp->xoc_col_fg) {
	cp += snprintf(cp, ep - cp, ";3%u",
		       (fg != XO_COL_DEFAULT) ? fg - 1 : 9);
	if (cp >= ep)
	    return;		/* Should not occur */
    }

    xo_color_t bg = newp->xoc_col_bg;
    if (bg != oldp->xoc_col_bg) {
	cp += snprintf(cp, ep - cp, ";4%u",
		       (bg != XO_COL_DEFAULT) ? bg - 1 : 9);
	if (cp >= ep)
	    return;		/* Should not occur */
    }

    if (cp - buf != 1 && cp < ep - 3) {
	buf[1] = '[';		/* Overwrite leading ';' */
	*cp++ = 'm';
	*cp = '\0';
	xo_buf_append(&xop->xo_data, buf, cp - buf);
    }
}

static void
xo_colors_handle_html (xo_handle_t *xop, xo_colors_t *newp)
{
    xo_colors_t *oldp = &xop->xo_colors;

    /*
     * HTML colors are mostly trivial: fill in xo_color_buf with
     * a set of class tags representing the colors and effects.
     */

    /* If nothing changed, then do nothing */
    if (oldp->xoc_effects == newp->xoc_effects
	&& oldp->xoc_col_fg == newp->xoc_col_fg
	&& oldp->xoc_col_bg == newp->xoc_col_bg)
	return;

    unsigned i, bit;
    xo_buffer_t *xbp = &xop->xo_color_buf;

    xo_buf_reset(xbp);		/* We rebuild content after each change */

    for (i = 0, bit = 1; xo_effect_names[i]; i++, bit <<= 1) {
	if (!(newp->xoc_effects & bit))
	    continue;

	xo_buf_append_str(xbp, " effect-");
	xo_buf_append_str(xbp, xo_effect_names[i]);
    }

    const char *fg = NULL;
    const char *bg = NULL;

    if (newp->xoc_col_fg != XO_COL_DEFAULT)
	fg = xo_color_names[newp->xoc_col_fg];
    if (newp->xoc_col_bg != XO_COL_DEFAULT)
	bg = xo_color_names[newp->xoc_col_bg];

    if (newp->xoc_effects & XO_EFF_INVERSE) {
	const char *tmp = fg;
	fg = bg;
	bg = tmp;
	if (fg == NULL)
	    fg = "inverse";
	if (bg == NULL)
	    bg = "inverse";

    }

    if (fg) {
	xo_buf_append_str(xbp, " color-fg-");
	xo_buf_append_str(xbp, fg);
    }

    if (bg) {
	xo_buf_append_str(xbp, " color-bg-");
	xo_buf_append_str(xbp, bg);
    }
}

static void
xo_format_colors (xo_handle_t *xop, xo_field_info_t *xfip,
		  const char *value, ssize_t vlen)
{
    const char *fmt = xfip->xfi_format;
    ssize_t flen = xfip->xfi_flen;

    xo_buffer_t xb;

    /* If the string is static and we've in an encoding style, bail */
    if (vlen != 0 && xo_style_is_encoding(xop))
	return;

    xo_buf_init(&xb);

    if (vlen)
	xo_buf_append(&xb, value, vlen);
    else if (flen)
	xo_do_format_field(xop, &xb, fmt, flen, 0);
    else
	xo_buf_append(&xb, "reset", 6); /* Default if empty */

    if (xo_colors_enabled(xop)) {
	switch (xo_style(xop)) {
	case XO_STYLE_TEXT:
	case XO_STYLE_HTML:
	    xo_buf_append(&xb, "", 1);

	    xo_colors_t xoc = xop->xo_colors;
	    xo_colors_parse(xop, &xoc, xb.xb_bufp);
	    xo_colors_update(xop, &xoc);

	    if (xo_style(xop) == XO_STYLE_TEXT) {
		/*
		 * Text mode means emitting the colors as ANSI character
		 * codes.  This will allow people who like colors to have
		 * colors.  The issue is, of course conflicting with the
		 * user's perfectly reasonable color scheme.  Which leads
		 * to the hell of LSCOLORS, where even app need to have
		 * customization hooks for adjusting colors.  Instead we
		 * provide a simpler-but-still-annoying answer where one
		 * can map colors to other colors.
		 */
		xo_colors_handle_text(xop, &xoc);
		xoc.xoc_effects &= ~XO_EFF_RESET; /* After handling it */

	    } else {
		/*
		 * HTML output is wrapped in divs, so the color information
		 * must appear in every div until cleared.  Most pathetic.
		 * Most unavoidable.
		 */
		xoc.xoc_effects &= ~XO_EFF_RESET; /* Before handling effects */
		xo_colors_handle_html(xop, &xoc);
	    }

	    xop->xo_colors = xoc;
	    break;

	case XO_STYLE_XML:
	case XO_STYLE_JSON:
	case XO_STYLE_SDPARAMS:
	case XO_STYLE_ENCODER:
	    /*
	     * Nothing to do; we did all that work just to clear the stack of
	     * formatting arguments.
	     */
	    break;
	}
    }

    xo_buf_cleanup(&xb);
}

static void
xo_format_units (xo_handle_t *xop, xo_field_info_t *xfip,
		 const char *value, ssize_t vlen)
{
    const char *fmt = xfip->xfi_format;
    ssize_t flen = xfip->xfi_flen;
    xo_xff_flags_t flags = xfip->xfi_flags;

    static char units_start_xml[] = " units=\"";
    static char units_start_html[] = " data-units=\"";

    if (!XOIF_ISSET(xop, XOIF_UNITS_PENDING)) {
	xo_format_content(xop, "units", NULL, value, vlen, fmt, flen, flags);
	return;
    }

    xo_buffer_t *xbp = &xop->xo_data;
    ssize_t start = xop->xo_units_offset;
    ssize_t stop = xbp->xb_curp - xbp->xb_bufp;

    if (xo_style(xop) == XO_STYLE_XML)
	xo_buf_append(xbp, units_start_xml, sizeof(units_start_xml) - 1);
    else if (xo_style(xop) == XO_STYLE_HTML)
	xo_buf_append(xbp, units_start_html, sizeof(units_start_html) - 1);
    else
	return;

    if (vlen)
	xo_data_escape(xop, value, vlen);
    else
	xo_do_format_field(xop, NULL, fmt, flen, flags);

    xo_buf_append(xbp, "\"", 1);

    ssize_t now = xbp->xb_curp - xbp->xb_bufp;
    ssize_t delta = now - stop;
    if (delta <= 0) {		/* Strange; no output to move */
	xo_buf_set_offset(xbp, stop); /* Reset buffer to prior state */
	return;
    }

    /*
     * Now we're in it alright.  We've need to insert the unit value
     * we just created into the right spot.  We make a local copy,
     * move it and then insert our copy.  We know there's room in the
     * buffer, since we're just moving this around.
     */
    char *buf = alloca(delta);

    memcpy(buf, xbp->xb_bufp + stop, delta);
    memmove(xbp->xb_bufp + start + delta, xbp->xb_bufp + start, stop - start);
    memmove(xbp->xb_bufp + start, buf, delta);
}

static ssize_t
xo_find_width (xo_handle_t *xop, xo_field_info_t *xfip,
	       const char *value, ssize_t vlen)
{
    const char *fmt = xfip->xfi_format;
    ssize_t flen = xfip->xfi_flen;

    long width = 0;
    char *bp;
    char *cp;

    if (vlen) {
	bp = alloca(vlen + 1);	/* Make local NUL-terminated copy of value */
	memcpy(bp, value, vlen);
	bp[vlen] = '\0';

	width = strtol(bp, &cp, 0);
	if (width == LONG_MIN || width == LONG_MAX || bp == cp || *cp != '\0') {
	    width = 0;
	    xo_failure(xop, "invalid width for anchor: '%s'", bp);
	}
    } else if (flen) {
	/*
	 * We really expect the format for width to be "{:/%d}" or
	 * "{:/%u}", so if that's the case, we just grab our width off
	 * the argument list.  But we need to avoid optimized logic if
	 * there's a custom formatter.
	 */
	if (xop->xo_formatter == NULL && flen == 2
	        && strncmp("%d", fmt, flen) == 0) {
	    if (!XOF_ISSET(xop, XOF_NO_VA_ARG))
		width = va_arg(xop->xo_vap, int);
	} else if (xop->xo_formatter == NULL && flen == 2
		   && strncmp("%u", fmt, flen) == 0) {
	    if (!XOF_ISSET(xop, XOF_NO_VA_ARG))
		width = va_arg(xop->xo_vap, unsigned);
	} else {
	    /*
	     * So we have a format and it's not a simple one like
	     * "{:/%d}".  That means we need to format the field,
	     * extract the value from the formatted output, and then
	     * discard that output.
	     */
	    int anchor_was_set = FALSE;
	    xo_buffer_t *xbp = &xop->xo_data;
	    ssize_t start_offset = xo_buf_offset(xbp);
	    bp = xo_buf_cur(xbp);	/* Save start of the string */
	    cp = NULL;

	    if (XOIF_ISSET(xop, XOIF_ANCHOR)) {
		XOIF_CLEAR(xop, XOIF_ANCHOR);
		anchor_was_set = TRUE;
	    }

	    ssize_t rc = xo_do_format_field(xop, xbp, fmt, flen, 0);
	    if (rc >= 0) {
		xo_buf_append(xbp, "", 1); /* Append a NUL */

		width = strtol(bp, &cp, 0);
		if (width == LONG_MIN || width == LONG_MAX
		        || bp == cp || *cp != '\0') {
		    width = 0;
		    xo_failure(xop, "invalid width for anchor: '%s'", bp);
		}
	    }

	    /* Reset the cur pointer to where we found it */
	    xo_buf_set_offset(xbp, start_offset);
	    if (anchor_was_set)
		XOIF_SET(xop, XOIF_ANCHOR);
	}
    }

    return width;
}

static void
xo_anchor_clear (xo_handle_t *xop)
{
    XOIF_CLEAR(xop, XOIF_ANCHOR);
    xop->xo_anchor_offset = 0;
    xop->xo_anchor_columns = 0;
    xop->xo_anchor_min_width = 0;
}

/*
 * An anchor is a marker used to delay field width implications.
 * Imagine the format string "{[:10}{min:%d}/{cur:%d}/{max:%d}{:]}".
 * We are looking for output like "     1/4/5"
 *
 * To make this work, we record the anchor and then return to
 * format it when the end anchor tag is seen.
 */
static void
xo_anchor_start (xo_handle_t *xop, xo_field_info_t *xfip,
		 const char *value, ssize_t vlen)
{
    if (XOIF_ISSET(xop, XOIF_ANCHOR))
	xo_failure(xop, "the anchor already recording is discarded");

    XOIF_SET(xop, XOIF_ANCHOR);
    xo_buffer_t *xbp = &xop->xo_data;
    xop->xo_anchor_offset = xbp->xb_curp - xbp->xb_bufp;
    xop->xo_anchor_columns = 0;

    /*
     * Now we find the width, if possible.  If it's not there,
     * we'll get it on the end anchor.
     */
    xop->xo_anchor_min_width = xo_find_width(xop, xfip, value, vlen);
}

static void
xo_anchor_stop (xo_handle_t *xop, xo_field_info_t *xfip,
		 const char *value, ssize_t vlen)
{
    if (!XOIF_ISSET(xop, XOIF_ANCHOR)) {
	xo_failure(xop, "no start anchor");
	return;
    }

    XOIF_CLEAR(xop, XOIF_UNITS_PENDING);

    ssize_t width = xo_find_width(xop, xfip, value, vlen);
    if (width == 0)
	width = xop->xo_anchor_min_width;

    if (width == 0)		/* No width given; nothing to do */
	goto done;

    xo_buffer_t *xbp = &xop->xo_data;
    ssize_t start = xop->xo_anchor_offset;
    ssize_t stop = xbp->xb_curp - xbp->xb_bufp;
    ssize_t abswidth = (width > 0) ? width : -width;
    ssize_t blen = abswidth - xop->xo_anchor_columns;

    if (blen <= 0)		/* Already over width */
	goto done;

    if (abswidth > XO_MAX_ANCHOR_WIDTH) {
	xo_failure(xop, "width over %u are not supported",
		   XO_MAX_ANCHOR_WIDTH);
	goto done;
    }

    /* Make a suitable padding field and emit it */
    char *buf = alloca(blen);
    memset(buf, ' ', blen);
    xo_format_content(xop, "padding", NULL, buf, blen, NULL, 0, 0);

    if (width < 0)		/* Already left justified */
	goto done;

    ssize_t now = xbp->xb_curp - xbp->xb_bufp;
    ssize_t delta = now - stop;
    if (delta <= 0)		/* Strange; no output to move */
	goto done;

    /*
     * Now we're in it alright.  We've need to insert the padding data
     * we just created (which might be an HTML <div> or text) before
     * the formatted data.  We make a local copy, move it and then
     * insert our copy.  We know there's room in the buffer, since
     * we're just moving this around.
     */
    if (delta > blen)
	buf = alloca(delta);	/* Expand buffer if needed */

    memcpy(buf, xbp->xb_bufp + stop, delta);
    memmove(xbp->xb_bufp + start + delta, xbp->xb_bufp + start, stop - start);
    memmove(xbp->xb_bufp + start, buf, delta);

 done:
    xo_anchor_clear(xop);
}

static const char *
xo_class_name (int ftype)
{
    switch (ftype) {
    case 'D': return "decoration";
    case 'E': return "error";
    case 'L': return "label";
    case 'N': return "note";
    case 'P': return "padding";
    case 'W': return "warning";
    }

    return NULL;
}

static const char *
xo_tag_name (int ftype)
{
    switch (ftype) {
    case 'E': return "__error";
    case 'W': return "__warning";
    }

    return NULL;
}

/*
 * Number any remaining fields that need numbers.  Note that some
 * field types (text, newline, escaped braces) never get numbers.
 */
static void
xo_gettext_finish_numbering_fields (xo_handle_t *xop UNUSED,
				    const char *fmt UNUSED,
				    xo_field_info_t *fields)
{
    xo_field_info_t *xfip;
    unsigned fnum, max_fields;
    uint64_t bits = 0;
    const uint64_t one = 1;	/* Avoid "1ULL" */

    /* First make a list of add the explicitly used bits */
    for (xfip = fields, fnum = 0; xfip->xfi_ftype; xfip++) {
	switch (xfip->xfi_ftype) {
	case XO_ROLE_NEWLINE:	/* Don't get numbered */
	case XO_ROLE_TEXT:
	case XO_ROLE_EBRACE:
	case 'G':
	    continue;
	}

	fnum += 1;
	if (fnum >= 63)
	    break;

	if (xfip->xfi_fnum)
	    bits |= one << xfip->xfi_fnum;
    }

    max_fields = fnum;

    for (xfip = fields, fnum = 0; xfip->xfi_ftype; xfip++) {
	switch (xfip->xfi_ftype) {
	case XO_ROLE_NEWLINE:	/* Don't get numbered */
	case XO_ROLE_TEXT:
	case XO_ROLE_EBRACE:
	case 'G':
	    continue;
	}

	if (xfip->xfi_fnum != 0)
	    continue;

	/* Find the next unassigned field */
	for (fnum++; bits & (one << fnum); fnum++)
	    continue;

	if (fnum > max_fields)
	    break;

	xfip->xfi_fnum = fnum;	/* Mark the field number */
	bits |= one << fnum;	/* Mark it used */
    }
}

/*
 * We are passed a pointer to a format string just past the "{G:}"
 * field.  We build a simplified version of the format string.
 */
static int
xo_gettext_simplify_format (xo_handle_t *xop UNUSED,
		       xo_buffer_t *xbp,
		       xo_field_info_t *fields,
		       int this_field,
		       const char *fmt UNUSED,
		       xo_simplify_field_func_t field_cb)
{
    unsigned ftype;
    xo_xff_flags_t flags;
    int field = this_field + 1;
    xo_field_info_t *xfip;
    char ch;

    for (xfip = &fields[field]; xfip->xfi_ftype; xfip++, field++) {
	ftype = xfip->xfi_ftype;
	flags = xfip->xfi_flags;

	if ((flags & XFF_GT_FIELD) && xfip->xfi_content && ftype != 'V') {
	    if (field_cb)
		field_cb(xfip->xfi_content, xfip->xfi_clen,
			 (flags & XFF_GT_PLURAL) ? 1 : 0);
	}

	switch (ftype) {
	case 'G':
	    /* Ignore gettext roles */
	    break;

	case XO_ROLE_NEWLINE:
	    xo_buf_append(xbp, "\n", 1);
	    break;

	case XO_ROLE_EBRACE:
	    xo_buf_append(xbp, "{", 1);
	    xo_buf_append(xbp, xfip->xfi_content, xfip->xfi_clen);
	    xo_buf_append(xbp, "}", 1);
	    break;

	case XO_ROLE_TEXT:
	    xo_buf_append(xbp, xfip->xfi_content, xfip->xfi_clen);
	    break;

	default:
	    xo_buf_append(xbp, "{", 1);
	    if (ftype != 'V') {
		ch = ftype;
		xo_buf_append(xbp, &ch, 1);
	    }

	    unsigned fnum = xfip->xfi_fnum ?: 0;
	    if (fnum) {
		char num[12];
		/* Field numbers are origin 1, not 0, following printf(3) */
		snprintf(num, sizeof(num), "%u", fnum);
		xo_buf_append(xbp, num, strlen(num));
	    }

	    xo_buf_append(xbp, ":", 1);
	    xo_buf_append(xbp, xfip->xfi_content, xfip->xfi_clen);
	    xo_buf_append(xbp, "}", 1);
	}
    }

    xo_buf_append(xbp, "", 1);
    return 0;
}

#ifdef LIBXO_DEBUG
void
xo_dump_fields (xo_field_info_t *); /* Fake prototype for debug function */
void
xo_dump_fields (xo_field_info_t *fields)
{
    xo_field_info_t *xfip;

    for (xfip = fields; xfip->xfi_ftype; xfip++) {
	printf("%lu(%u): %lx [%c/%u] [%.*s] [%.*s] [%.*s]\n",
	       (unsigned long) (xfip - fields), xfip->xfi_fnum,
	       (unsigned long) xfip->xfi_flags,
	       isprint((int) xfip->xfi_ftype) ? xfip->xfi_ftype : ' ',
	       xfip->xfi_ftype,
	       (int) xfip->xfi_clen, xfip->xfi_content ?: "", 
	       (int) xfip->xfi_flen, xfip->xfi_format ?: "", 
	       (int) xfip->xfi_elen, xfip->xfi_encoding ?: "");
    }
}
#endif /* LIBXO_DEBUG */


#ifdef HAVE_GETTEXT
/*
 * Find the field that matches the given field number
 */
static xo_field_info_t *
xo_gettext_find_field (xo_field_info_t *fields, unsigned fnum)
{
    xo_field_info_t *xfip;

    for (xfip = fields; xfip->xfi_ftype; xfip++)
	if (xfip->xfi_fnum == fnum)
	    return xfip;

    return NULL;
}

/*
 * At this point, we need to consider if the fields have been reordered,
 * such as "The {:adjective} {:noun}" to "La {:noun} {:adjective}".
 *
 * We need to rewrite the new_fields using the old fields order,
 * so that we can render the message using the arguments as they
 * appear on the stack.  It's a lot of work, but we don't really
 * want to (eventually) fall into the standard printf code which
 * means using the arguments straight (and in order) from the
 * varargs we were originally passed.
 */
static void
xo_gettext_rewrite_fields (xo_handle_t *xop UNUSED,
			   xo_field_info_t *fields, unsigned max_fields)
{
    xo_field_info_t tmp[max_fields];
    bzero(tmp, max_fields * sizeof(tmp[0]));

    unsigned fnum = 0;
    xo_field_info_t *newp, *outp, *zp;
    for (newp = fields, outp = tmp; newp->xfi_ftype; newp++, outp++) {
	switch (newp->xfi_ftype) {
	case XO_ROLE_NEWLINE:	/* Don't get numbered */
	case XO_ROLE_TEXT:
	case XO_ROLE_EBRACE:
	case 'G':
	    *outp = *newp;
	    outp->xfi_renum = 0;
	    continue;
	}

	zp = xo_gettext_find_field(fields, ++fnum);
	if (zp == NULL) { 	/* Should not occur */
	    *outp = *newp;
	    outp->xfi_renum = 0;
	    continue;
	}

	*outp = *zp;
	outp->xfi_renum = newp->xfi_fnum;
    }

    memcpy(fields, tmp, max_fields * sizeof(tmp[0]));
}

/*
 * We've got two lists of fields, the old list from the original
 * format string and the new one from the parsed gettext reply.  The
 * new list has the localized words, where the old list has the
 * formatting information.  We need to combine them into a single list
 * (the new list).
 *
 * If the list needs to be reordered, then we've got more serious work
 * to do.
 */
static int
xo_gettext_combine_formats (xo_handle_t *xop, const char *fmt UNUSED,
		    const char *gtfmt, xo_field_info_t *old_fields,
		    xo_field_info_t *new_fields, unsigned new_max_fields,
		    int *reorderedp)
{
    int reordered = 0;
    xo_field_info_t *newp, *oldp, *startp = old_fields;

    xo_gettext_finish_numbering_fields(xop, fmt, old_fields);

    for (newp = new_fields; newp->xfi_ftype; newp++) {
	switch (newp->xfi_ftype) {
	case XO_ROLE_NEWLINE:
	case XO_ROLE_TEXT:
	case XO_ROLE_EBRACE:
	    continue;

	case 'V':
	    for (oldp = startp; oldp->xfi_ftype; oldp++) {
		if (oldp->xfi_ftype != 'V')
		    continue;
		if (newp->xfi_clen != oldp->xfi_clen
		    || strncmp(newp->xfi_content, oldp->xfi_content,
			       oldp->xfi_clen) != 0) {
		    reordered = 1;
		    continue;
		}
		startp = oldp + 1;
		break;
	    }

	    /* Didn't find it on the first pass (starting from start) */
	    if (oldp->xfi_ftype == 0) {
		for (oldp = old_fields; oldp < startp; oldp++) {
		    if (oldp->xfi_ftype != 'V')
			continue;
		    if (newp->xfi_clen != oldp->xfi_clen)
			continue;
		    if (strncmp(newp->xfi_content, oldp->xfi_content,
				oldp->xfi_clen) != 0)
			continue;
		    reordered = 1;
		    break;
		}
		if (oldp == startp) {
		    /* Field not found */
		    xo_failure(xop, "post-gettext format can't find field "
			       "'%.*s' in format '%s'",
			       newp->xfi_clen, newp->xfi_content,
			       xo_printable(gtfmt));
		    return -1;
		}
	    }
	    break;

	default:
	    /*
	     * Other fields don't have names for us to use, so if
	     * the types aren't the same, then we'll have to assume
	     * the original field is a match.
	     */
	    for (oldp = startp; oldp->xfi_ftype; oldp++) {
		if (oldp->xfi_ftype == 'V') /* Can't go past these */
		    break;
		if (oldp->xfi_ftype == newp->xfi_ftype)
		    goto copy_it; /* Assumably we have a match */
	    }
	    continue;
	}

	/*
	 * Found a match; copy over appropriate fields
	 */
    copy_it:
	newp->xfi_flags = oldp->xfi_flags;
	newp->xfi_fnum = oldp->xfi_fnum;
	newp->xfi_format = oldp->xfi_format;
	newp->xfi_flen = oldp->xfi_flen;
	newp->xfi_encoding = oldp->xfi_encoding;
	newp->xfi_elen = oldp->xfi_elen;
    }

    *reorderedp = reordered;
    if (reordered) {
	xo_gettext_finish_numbering_fields(xop, fmt, new_fields);
	xo_gettext_rewrite_fields(xop, new_fields, new_max_fields);
    }

    return 0;
}

/*
 * We don't want to make gettext() calls here with a complete format
 * string, since that means changing a flag would mean a
 * labor-intensive re-translation expense.  Instead we build a
 * simplified form with a reduced level of detail, perform a lookup on
 * that string and then re-insert the formating info.
 *
 * So something like:
 *   xo_emit("{G:}close {:fd/%ld} returned {g:error/%m} {:test/%6.6s}\n", ...)
 * would have a lookup string of:
 *   "close {:fd} returned {:error} {:test}\n"
 *
 * We also need to handling reordering of fields, where the gettext()
 * reply string uses fields in a different order than the original
 * format string:
 *   "cluse-a {:fd} retoorned {:test}.  Bork {:error} Bork. Bork.\n"
 * If we have to reorder fields within the message, then things get
 * complicated.  See xo_gettext_rewrite_fields.
 *
 * Summary: i18n aighn't cheap.
 */
static const char *
xo_gettext_build_format (xo_handle_t *xop,
			 xo_field_info_t *fields, int this_field,
			 const char *fmt, char **new_fmtp)
{
    xo_buffer_t xb;
    xo_buf_zero(&xb);

    do {
	if (xo_style_is_encoding(xop))
	    break;

	if (xo_gettext_simplify_format(xop, &xb, fields,
				       this_field, fmt, NULL))
	    break;

	const char *gtfmt = xo_dgettext(xop, xb.xb_bufp);
	if (gtfmt == NULL || gtfmt == fmt || xo_streq(gtfmt, fmt))
	    break;

	char *new_fmt = xo_strndup(gtfmt, -1);
	if (new_fmt == NULL)
	    break;

	xo_buf_cleanup(&xb);

	*new_fmtp = new_fmt;
	return new_fmt;

    } while (FALSE);		/* Not really a loop at all */

    xo_buf_cleanup(&xb);
    *new_fmtp = NULL;
    return fmt;
}

static void
xo_gettext_rebuild_content (xo_handle_t *xop, xo_field_info_t *fields,
			    ssize_t *fstart, unsigned min_fstart,
			    ssize_t *fend, unsigned max_fend)
{
    xo_field_info_t *xfip;
    char *buf;
    ssize_t base = fstart[min_fstart];
    ssize_t blen = fend[max_fend] - base;
    xo_buffer_t *xbp = &xop->xo_data;

    if (blen == 0)
	return;

    buf = xo_realloc(NULL, blen);
    if (buf == NULL)
	return;

    memcpy(buf, xbp->xb_bufp + fstart[min_fstart], blen); /* Copy our data */

    unsigned field = min_fstart, len, fnum;
    ssize_t soff, doff = base;
    xo_field_info_t *zp;

    /*
     * Be aware there are two competing views of "field number": we
     * want the user to thing in terms of "The {1:size}" where {G:},
     * newlines, escaped braces, and text don't have numbers.  But is
     * also the internal view, where we have an array of
     * xo_field_info_t and every field have an index.  fnum, fstart[]
     * and fend[] are the latter, but xfi_renum is the former.
     */
    for (xfip = fields + field; xfip->xfi_ftype; xfip++, field++) {
	fnum = field;
	if (xfip->xfi_renum) {
	    zp = xo_gettext_find_field(fields, xfip->xfi_renum);
	    fnum = zp ? zp - fields : field;
	}

	soff = fstart[fnum];
	len = fend[fnum] - soff;

	if (len > 0) {
	    soff -= base;
	    memcpy(xbp->xb_bufp + doff, buf + soff, len);
	    doff += len;
	}
    }

    xo_free(buf);
}
#else  /* HAVE_GETTEXT */
static const char *
xo_gettext_build_format (xo_handle_t *xop UNUSED,
			 xo_field_info_t *fields UNUSED,
			 int this_field UNUSED,
			 const char *fmt UNUSED, char **new_fmtp)
{
    *new_fmtp = NULL;
    return fmt;
}

static int
xo_gettext_combine_formats (xo_handle_t *xop UNUSED, const char *fmt UNUSED,
		    const char *gtfmt UNUSED,
		    xo_field_info_t *old_fields UNUSED,
		    xo_field_info_t *new_fields UNUSED,
		    unsigned new_max_fields UNUSED,
		    int *reorderedp UNUSED)
{
    return -1;
}

static void
xo_gettext_rebuild_content (xo_handle_t *xop UNUSED,
		    xo_field_info_t *fields UNUSED,
		    ssize_t *fstart UNUSED, unsigned min_fstart UNUSED,
		    ssize_t *fend UNUSED, unsigned max_fend UNUSED)
{
    return;
}
#endif /* HAVE_GETTEXT */

/*
 * Emit a set of fields.  This is really the core of libxo.
 */
static ssize_t
xo_do_emit_fields (xo_handle_t *xop, xo_field_info_t *fields,
		   unsigned max_fields, const char *fmt)
{
    int gettext_inuse = 0;
    int gettext_changed = 0;
    int gettext_reordered = 0;
    unsigned ftype;
    xo_xff_flags_t flags;
    xo_xff_flags_t has_keys = 0;
    xo_field_info_t *new_fields = NULL;
    xo_field_info_t *xfip;
    unsigned field;
    ssize_t rc = 0;

    int flush = XOF_ISSET(xop, XOF_FLUSH);
    int flush_line = XOF_ISSET(xop, XOF_FLUSH_LINE);
    char *new_fmt = NULL;

    if (XOIF_ISSET(xop, XOIF_REORDER) || xo_style(xop) == XO_STYLE_ENCODER)
	flush_line = 0;
    else if (xo_avoid_flushing(xop))
	flush_line = 0;

    /*
     * Some overhead for gettext; if the fields in the msgstr returned
     * by gettext are reordered, then we need to record start and end
     * for each field.  We'll go ahead and render the fields in the
     * normal order, but later we can then reconstruct the reordered
     * fields using these fstart/fend values.
     */
    unsigned flimit = max_fields * 2; /* Pessimistic limit */
    unsigned min_fstart = flimit - 1;
    unsigned max_fend = 0;	      /* Highest recorded fend[] entry */
    ssize_t fstart[flimit];
    bzero(fstart, flimit * sizeof(fstart[0]));
    ssize_t fend[flimit];
    bzero(fend, flimit * sizeof(fend[0]));

    for (xfip = fields, field = 0; field < max_fields && xfip->xfi_ftype;
	 xfip++, field++) {
	ftype = xfip->xfi_ftype;
	flags = xfip->xfi_flags;

	/* Record field start offset */
	if (gettext_reordered) {
	    fstart[field] = xo_buf_offset(&xop->xo_data);
	    if (min_fstart > field)
		min_fstart = field;
	}

	const char *content = xfip->xfi_content;
	ssize_t clen = xfip->xfi_clen;

	if (flags & XFF_ARGUMENT) {
	    /*
	     * Argument flag means the content isn't given in the descriptor,
	     * but as a UTF-8 string ('const char *') argument in xo_vap.
	     */
            if (clen)
                xo_failure(xop, "invalid content value for 'a' modifier: '%.*s'",
                            clen, content);
	    content = va_arg(xop->xo_vap, char *);
	    clen = content ? strlen(content) : 0;
	}

	if (ftype == XO_ROLE_NEWLINE) {
	    xo_line_close(xop);
	    if (flush_line && xo_flush_h(xop) < 0)
		return -1;
	    goto bottom;

	} else if (ftype == XO_ROLE_EBRACE) {
	    xo_format_text(xop, xfip->xfi_start, xfip->xfi_len);
	    goto bottom;

	} else if (ftype == XO_ROLE_TEXT) {
	    /* Normal text */
	    xo_format_text(xop, xfip->xfi_content, xfip->xfi_clen);
	    goto bottom;
	}

	/*
	 * Notes and units need the 'w' flag handled before the content.
	 */
	if (ftype == 'N' || ftype == 'U') {
	    if (flags & XFF_WS) {
		xo_format_content(xop, "padding", NULL, " ", 1,
				  NULL, 0, flags);
		flags &= ~XFF_WS; /* Prevent later handling of this flag */
	    }
	}

	if (ftype == 'V')
	    xo_format_value(xop, content, clen, NULL, 0,
			    xfip->xfi_format, xfip->xfi_flen,
			    xfip->xfi_encoding, xfip->xfi_elen, flags);
	else if (ftype == '[')
	    xo_anchor_start(xop, xfip, content, clen);
	else if (ftype == ']')
	    xo_anchor_stop(xop, xfip, content, clen);
	else if (ftype == 'C')
	    xo_format_colors(xop, xfip, content, clen);

	else if (ftype == 'G') {
	    /*
	     * A {G:domain} field; disect the domain name and translate
	     * the remaining portion of the input string.  If the user
	     * didn't put the {G:} at the start of the format string, then
	     * assumably they just want us to translate the rest of it.
	     * Since gettext returns strings in a static buffer, we make
	     * a copy in new_fmt.
	     */
	    xo_set_gettext_domain(xop, xfip, content, clen);

	    if (!gettext_inuse) { /* Only translate once */
		gettext_inuse = 1;
		if (new_fmt) {
		    xo_free(new_fmt);
		    new_fmt = NULL;
		}

		xo_gettext_build_format(xop, fields, field,
					xfip->xfi_next, &new_fmt);
		if (new_fmt) {
		    gettext_changed = 1;

		    xo_parse_t nxpp;
		    xo_parse_for_handle(xop, &nxpp);

		    unsigned new_max_fields = xo_count_fields(&nxpp, new_fmt);

		    if (++new_max_fields < max_fields)
			new_max_fields = max_fields;

		    /* Leave a blank slot at the beginning */
		    ssize_t sz = (new_max_fields + 1) * sizeof(xo_field_info_t);
		    new_fields = alloca(sz);
		    bzero(new_fields, sz);

		    if (!xo_parse_fields(&nxpp, new_fields + 1,
					 new_max_fields, new_fmt)) {
			gettext_reordered = 0;

			if (!xo_gettext_combine_formats(xop, fmt, new_fmt,
					fields, new_fields + 1,
					new_max_fields, &gettext_reordered)) {

			    if (gettext_reordered) {
				if (XOF_ISSET(xop, XOF_LOG_GETTEXT))
				    xo_failure(xop, "gettext finds reordered "
					       "fields in '%s' and '%s'",
					       xo_printable(fmt),
					       xo_printable(new_fmt));
				flush_line = 0; /* Must keep at content */
				XOIF_SET(xop, XOIF_REORDER);
			    }

			    field = -1; /* Will be incremented at top of loop */
			    xfip = new_fields;
			    max_fields = new_max_fields;
			}
		    }
		}
	    }
	    continue;

	} else  if (clen || xfip->xfi_format) {

	    const char *class_name = xo_class_name(ftype);
	    if (class_name)
		xo_format_content(xop, class_name, xo_tag_name(ftype),
				  content, clen,
				  xfip->xfi_format, xfip->xfi_flen, flags);
	    else if (ftype == 'T')
		xo_format_title(xop, xfip, content, clen);
	    else if (ftype == 'U')
		xo_format_units(xop, xfip, content, clen);
	    else
		xo_failure(xop, "unknown field type: '%c'", ftype);
	}

	if (flags & XFF_COLON)
	    xo_format_content(xop, "decoration", NULL, ":", 1, NULL, 0, 0);

	if (flags & XFF_WS)
	    xo_format_content(xop, "padding", NULL, " ", 1, NULL, 0, 0);

    bottom:
	/* Record the end-of-field offset */
	if (gettext_reordered) {
	    fend[field] = xo_buf_offset(&xop->xo_data);
	    max_fend = field;
	}

	has_keys |= (flags & XFF_KEY);
    }

    if (XOIF_ISSET(xop, XOIF_FILTERING)) {
	/*
	 * If we're filtering, we can look at the fields to see if we
	 * have any keys.  If we don't we can bail.
	 */
	if (has_keys == 0)
	    return 0;
    }

    if (gettext_changed && gettext_reordered) {
	/* Final step: rebuild the content using the rendered fields */
	xo_gettext_rebuild_content(xop, new_fields + 1, fstart, min_fstart,
				   fend, max_fend);
    }

    XOIF_CLEAR(xop, XOIF_REORDER);

    /*
     * If we've got enough data, flush it.
     */
    if (xo_buf_offset(&xop->xo_data) > XO_BUF_HIGH_WATER)
	flush = 1;

    /* If we don't have an anchor, write the text out */
    if (flush && !xo_avoid_flushing(xop)) {
	if (xo_flush_h(xop) < 0)
	    rc = -1;
    }

    if (new_fmt)
	xo_free(new_fmt);

    /*
     * We've carried the gettext domainname inside our handle just for
     * convenience, but we need to ensure it doesn't survive across
     * xo_emit calls.
     */
    if (xop->xo_gt_domain) {
	xo_free(xop->xo_gt_domain);
	xop->xo_gt_domain = NULL;
    }

    return (rc < 0) ? rc : xop->xo_columns;
}

/*
 * Parse and emit a set of fields
 */
static int
xo_do_emit (xo_handle_t *xop, xo_emit_flags_t flags, const char *fmt)
{
    xop->xo_columns = 0;	/* Always reset it */
    xop->xo_errno = errno;	/* Save for "%m" */

    if (fmt == NULL)
	return 0;

    /* Don't bother emitting fields is there's we're discarding output */
    if (xo_discarding_output_h(xop))
	return 0;		/* Zero columns emitted */

    unsigned max_fields;
    xo_field_info_t *fields = NULL;

    /* Adjust XOEF_RETAIN based on global flags */
    if (flags & XOEF_NO_RETAIN) {
	/* If the "don't retain flag is on, remove the retain, just in case */
	flags &= ~XOEF_RETAIN;

    } else if (flags & XOEF_RETAIN) {
	/* If the user doesn't want to retain, even if the caller does */
	if (XOF_ISSET(xop, XOF_RETAIN_NONE))
	    flags &= ~XOEF_RETAIN;
    } else if (!xo_str_is_const(fmt)) {
	/*
	 * Unless the caller explicitly tells us otherwise, we can
	 * only retain (cache) const strings, since dynamic strings
	 * aren't cachable due to changing content.
	 */
	/* Do nothing */
    } else if (XOF_ISSET(xop, XOF_RETAIN_ALL)) {
	/* If the user wants to retain allow it */
	flags |= XOEF_RETAIN;
    }

    /*
     * Check for 'retain' flag, telling us to retain the field
     * information.  If we've already saved it, then we can avoid
     * re-parsing the format string.
     * Dynamically build formats must tell us that the format is
     * dynamic using the XOEF_NO_RETAIN flag.
     */
    if (!(flags & XOEF_RETAIN)
	|| xo_retain_find(fmt, &fields, &max_fields) != 0
	|| fields == NULL) {

	/* Nothing retained; parse the format string */
	xo_parse_t xpp;
	xo_parse_for_handle(xop, &xpp);
	max_fields = xo_count_fields(&xpp, fmt);
	fields = alloca(max_fields * sizeof(fields[0]));
	bzero(fields, max_fields * sizeof(fields[0]));

	if (xo_parse_fields(&xpp, fields, max_fields, fmt))
	    return -1;		/* Warning already displayed */

	if (flags & XOEF_RETAIN) {
	    /* Retain the info */
	    xo_retain_add(fmt, fields, max_fields);
	}
    }

    return xo_do_emit_fields(xop, fields, max_fields, fmt);
}

/*
 * Rebuild a format string in a gettext-friendly format.  This function
 * is exposed to tools can perform this function.  See xo(1).
 */
char *
xo_simplify_format (xo_handle_t *xop, const char *fmt, int with_numbers,
		    xo_simplify_field_func_t field_cb)
{
    xop = xo_default(xop);

    xop->xo_columns = 0;	/* Always reset it */
    xop->xo_errno = errno;	/* Save for "%m" */

    xo_parse_t xpp;
    xo_parse_for_handle(xop, &xpp);
    unsigned max_fields = xo_count_fields(&xpp, fmt);
    xo_field_info_t fields[max_fields];

    bzero(fields, max_fields * sizeof(fields[0]));

    if (xo_parse_fields(&xpp, fields, max_fields, fmt))
	return NULL;		/* Warning already displayed */

    xo_buffer_t xb;
    xo_buf_init(&xb);

    if (with_numbers)
	xo_gettext_finish_numbering_fields(xop, fmt, fields);

    if (xo_gettext_simplify_format(xop, &xb, fields, -1, fmt, field_cb))
	return NULL;

    return xb.xb_bufp;
}

xo_ssize_t
xo_emit_hv (xo_handle_t *xop, const char *fmt, va_list vap)
{
    ssize_t rc;

    xop = xo_default(xop);
    va_copy(xop->xo_vap, vap);
    rc = xo_do_emit(xop, 0, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emit_h (xo_handle_t *xop, const char *fmt, ...)
{
    ssize_t rc;

    xop = xo_default(xop);
    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, 0, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emit (const char *fmt, ...)
{
    xo_handle_t *xop = xo_default(NULL);
    ssize_t rc;

    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, 0, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emitr (const char *fmt, ...)
{
    xo_handle_t *xop = xo_default(NULL);
    ssize_t rc;

    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, XOEF_RETAIN, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emit_hvf (xo_handle_t *xop, xo_emit_flags_t flags,
	     const char *fmt, va_list vap)
{
    ssize_t rc;

    xop = xo_default(xop);
    va_copy(xop->xo_vap, vap);
    rc = xo_do_emit(xop, flags, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emit_hf (xo_handle_t *xop, xo_emit_flags_t flags, const char *fmt, ...)
{
    ssize_t rc;

    xop = xo_default(xop);
    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, flags, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

xo_ssize_t
xo_emit_f (xo_emit_flags_t flags, const char *fmt, ...)
{
    xo_handle_t *xop = xo_default(NULL);
    ssize_t rc;

    va_start(xop->xo_vap, fmt);
    rc = xo_do_emit(xop, flags, fmt);
    va_end(xop->xo_vap);
    bzero(&xop->xo_vap, sizeof(xop->xo_vap));

    return rc;
}

/*
 * Emit a single field by providing the info information typically provided
 * inside the field description (role, modifiers, and formats).  This is
 * a convenience function to avoid callers using snprintf to build field
 * descriptions.
 */
xo_ssize_t
xo_emit_field_hvf (xo_handle_t *xop, xo_emit_flags_t flags UNUSED,
		   const char *rolmod, const char *contents,
		   const char *fmt, const char *efmt,
		   va_list vap)
{
    ssize_t rc;

    xop = xo_default(xop);

    if (rolmod == NULL)
	rolmod = "V";

    xo_field_info_t xfi;

    bzero(&xfi, sizeof(xfi));

    xo_parse_t xpp;
    xo_parse_for_handle(xop, &xpp);

    const char *cp;
    cp = xo_parse_roles(&xpp, rolmod, rolmod, &xfi);
    if (cp == NULL)
	return -1;

    xfi.xfi_start = fmt;
    xfi.xfi_content = contents;
    xfi.xfi_format = fmt;
    xfi.xfi_encoding = efmt;
    xfi.xfi_clen = contents ? strlen(contents) : 0;
    xfi.xfi_flen = fmt ? strlen(fmt) : 0;
    xfi.xfi_elen = efmt ? strlen(efmt) : 0;

    /* If we have content, then we have a default format */
    if (contents && fmt == NULL
		&& xo_role_wants_default_format(xfi.xfi_ftype)) {
	xfi.xfi_format = xo_default_format;
	xfi.xfi_flen = 2;
    }

    va_copy(xop->xo_vap, vap);

    rc = xo_do_emit_fields(xop, &xfi, 1, fmt ?: contents ?: "field");

    va_end(xop->xo_vap);

    return rc;
}

xo_ssize_t
xo_emit_field_hv (xo_handle_t *xop, const char *rolmod, const char *contents,
		  const char *fmt, const char *efmt,
		  va_list vap)
{
    return xo_emit_field_hvf(xop, 0, rolmod, contents, fmt, efmt, vap);
}

xo_ssize_t
xo_emit_field_h (xo_handle_t *xop, const char *rolmod, const char *contents,
		 const char *fmt, const char *efmt, ...)
{
    ssize_t rc;
    va_list vap;

    va_start(vap, efmt);
    rc = xo_emit_field_hvf(xop, 0, rolmod, contents, fmt, efmt, vap);
    va_end(vap);

    return rc;
}

xo_ssize_t
xo_emit_field_f (xo_emit_flags_t flags, const char *rolmod,
		 const char *contents,
		 const char *fmt, const char *efmt, ...)
{
    xo_handle_t *xop = xo_default(NULL);

    ssize_t rc;
    va_list vap;

    va_start(vap, efmt);
    rc = xo_emit_field_hvf(xop, flags, rolmod, contents, fmt, efmt, vap);
    va_end(vap);

    return rc;
}

xo_ssize_t
xo_emit_field (const char *rolmod, const char *contents,
	       const char *fmt, const char *efmt, ...)
{
    ssize_t rc;
    va_list vap;

    va_start(vap, efmt);
    rc = xo_emit_field_hvf(NULL, 0, rolmod, contents, fmt, efmt, vap);
    va_end(vap);

    return rc;
}

xo_ssize_t
xo_attr_hv (xo_handle_t *xop, const char *name, const char *fmt, va_list vap)
{
    const ssize_t extra = 5; 	/* space, equals, quote, quote, and nul */
    xop = xo_default(xop);

    ssize_t rc = 0;
    ssize_t nlen = strlen(name);
    xo_buffer_t *xbp = &xop->xo_attrs;
    ssize_t name_offset, value_offset;

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	if (xo_check_for_room(xop, xbp, nlen + extra))
	    return -1;

	*xbp->xb_curp++ = ' ';
	memcpy(xbp->xb_curp, name, nlen);
	xbp->xb_curp += nlen;
	*xbp->xb_curp++ = '=';
	*xbp->xb_curp++ = '"';

	rc = xo_vsnprintf(xop, xbp, fmt, vap);

	if (rc >= 0) {
	    if (XOF_ISSET(xop, XOF_FILTER))
		xo_filter_attribute(xop, xo_filters(xop),
				    name, nlen, xbp->xb_curp, rc);
	    rc = xo_escape_xml(xop, xbp, rc, 1);
	    xbp->xb_curp += rc;
	}

	if (xo_check_for_room(xop, xbp, 2))
	    return -1;

	*xbp->xb_curp++ = '"';
	*xbp->xb_curp = '\0';

	rc += nlen + extra;
	break;

    case XO_STYLE_ENCODER:
	name_offset = xo_buf_offset(xbp);
	xo_buf_append(xbp, name, nlen);
	xo_buf_append(xbp, "", 1);

	value_offset = xo_buf_offset(xbp);
	rc = xo_vsnprintf(xop, xbp, fmt, vap);
	if (rc >= 0) {
	    xbp->xb_curp += rc;
	    *xbp->xb_curp = '\0';
	    rc = xo_encoder_handle(xop, XO_OP_ATTRIBUTE, NULL,
				   xo_buf_data(xbp, name_offset),
				   xo_buf_data(xbp, value_offset), 0);
	}
	break;

    default:
	if (XOF_ISSET(xop, XOF_FILTER)) {
	    rc = xo_vsnprintf(xop, xbp, fmt, vap);
	    if (rc >= 0)
		xo_filter_attribute(xop, xo_filters(xop),
				    name, nlen, xbp->xb_curp, rc);
	    rc = 0; /* Value written to xbp as scratch; don't advance */
	}
	break;
    }

    return rc;
}

xo_ssize_t
xo_attr_h (xo_handle_t *xop, const char *name, const char *fmt, ...)
{
    ssize_t rc;
    va_list vap;

    va_start(vap, fmt);
    rc = xo_attr_hv(xop, name, fmt, vap);
    va_end(vap);

    return rc;
}

xo_ssize_t
xo_attr (const char *name, const char *fmt, ...)
{
    ssize_t rc;
    va_list vap;

    va_start(vap, fmt);
    rc = xo_attr_hv(NULL, name, fmt, vap);
    va_end(vap);

    return rc;
}

static void
xo_depth_change (xo_handle_t *xop, const char *name,
		 int delta, int indent, xo_state_t state,
		 xo_xsf_flags_t flags, xo_filter_status_t fstatus,
		 xo_off_t starting_offset)
{
    if (xo_style(xop) == XO_STYLE_HTML || xo_style(xop) == XO_STYLE_TEXT)
	indent = 0;

    if (XOF_ISSET(xop, XOF_DTRT))
	flags |= XSF_DTRT;

    if (delta >= 0) {			/* Push operation */
	if (xo_depth_check(xop, xop->xo_depth + delta))
	    return;

	xo_stack_t *xsp = &xop->xo_stack[xop->xo_depth + delta];
	xsp->xs_flags = flags;
	xsp->xs_state = state;
	xsp->xs_fstatus = fstatus;
	xsp->xs_rb_off = starting_offset;
	xsp->xs_tag_end = XS_OFFSET_CLEAR;
	xsp->xs_rb_flags = xop->xo_rb_snap; /* parent flags before this open */
	xop->xo_rb_snap = 0;
	xo_stack_set_flags(xop);

	XO_DBG(xop, "xo_depth_change: '%s' depth %d, state %u=%s, "
	       "status %u=%s,  rb_off %d, rb_flags %#x",
	       name, xop->xo_depth + delta,
	       state, xo_state_name(state),
	       fstatus, xo_filt_status_name(fstatus),
	       (int) starting_offset, xsp->xs_rb_flags);

	if (name == NULL)
	    name = XO_FAILURE_NAME;

	xsp->xs_name = xo_strndup(name, -1);

    } else {			/* Pop operation */
	if (xop->xo_depth == 0) {
	    if (!XOF_ISSET(xop, XOF_IGNORE_CLOSE))
		xo_failure(xop, "close with empty stack: '%s'", name);
	    return;
	}

	xo_stack_t *xsp = xo_stack_cur(xop);
	if (XOF_ISSET(xop, XOF_WARN)) {
	    const char *top = xsp->xs_name;
	    if (top != NULL && name != NULL && !xo_streq(name, top)) {
		xo_failure(xop, "incorrect close: '%s' .vs. '%s'",
			      name, top);
		return;
	    } 
	    if ((xsp->xs_flags & XSF_LIST) != (flags & XSF_LIST)) {
		xo_failure(xop, "list close on list confict: '%s'",
			      name);
		return;
	    }
	    if ((xsp->xs_flags & XSF_INSTANCE) != (flags & XSF_INSTANCE)) {
		xo_failure(xop, "list close on instance confict: '%s'",
			      name);
		return;
	    }
	}

	/* Clear any offsets */
	xsp->xs_rb_off = XS_OFFSET_CLEAR;
	xsp->xs_tag_end = XS_OFFSET_CLEAR;
	xsp->xs_key_off = XS_OFFSET_CLEAR;

	if (xsp->xs_name) {
	    xo_free(xsp->xs_name);
	    xsp->xs_name = NULL;
	}
	if (xsp->xs_keys) {
	    xo_free(xsp->xs_keys);
	    xsp->xs_keys = NULL;
	}
    }

    xop->xo_depth += delta;	/* Record new depth */
    xop->xo_indent += indent;
}

void
xo_set_depth (xo_handle_t *xop, int depth)
{
    xop = xo_default(xop);

    if (xo_depth_check(xop, depth))
	return;

    xop->xo_depth += depth;
    xop->xo_indent += depth;

    /*
     * Handling the "top wrapper" for JSON is a bit of a pain.  Here
     * we need to detect that the depth has been changed to set the
     * "XOIF_TOP_EMITTED" flag correctly.
     */
    if (xop->xo_style == XO_STYLE_JSON
	&& !XOF_ISSET(xop, XOF_NO_TOP) && xop->xo_depth > 0)
	XOIF_SET(xop, XOIF_TOP_EMITTED);
}

static xo_xsf_flags_t
xo_stack_flags (xo_xof_flags_t xflags)
{
    if (xflags & XOF_DTRT)
	return XSF_DTRT;
    return 0;
}

static void
xo_emit_top (xo_handle_t *xop, const char *ppn)
{
    xo_printf(xop, "%*s{%s", xo_indent(xop), "", ppn);
    XOIF_SET(xop, XOIF_TOP_EMITTED);

    if (xop->xo_version) {
	xo_printf(xop, "%*s\"__version\": \"%s\", %s",
		  xo_indent(xop), "", xop->xo_version, ppn);
	xo_free(xop->xo_version);
	xop->xo_version = NULL;
    }
}

static ssize_t
xo_do_open_container (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    ssize_t rc = 0;
    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    if (name == NULL) {
	xo_failure(xop, "NULL passed for container name");
	name = XO_FAILURE_NAME;
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_filter_status_t fstatus;
    fstatus = xo_filter_open_container(xop, xo_filters(xop), name);

    xo_stack_t *xsp = xo_stack_cur(xop);
    xo_filter_status_t old_fstatus = xsp->xs_fstatus;

    const char *leader = xo_xml_leader(xop, name);
    flags |= xop->xo_flags;	/* Pick up handle flags */

    /* Save the starting point, so depth_change can record it later */
    xo_off_t starting_offset = xo_buf_offset(&xop->xo_data);
    xop->xo_rb_snap = xop->xo_stack[xop->xo_depth].xs_flags & XSF_RB_BITS;

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	if (fstatus == XO_STATUS_DEAD) /* No one wants this */
	    break;

	/*
	 * If we are newly "full", then we need all our parents to be emitted
	 */
	xo_filt_handle_change_status(xop, old_fstatus, fstatus);

	rc = xo_printf(xop, "%*s<%s%s", xo_indent(xop), "", leader, name);

	if (xop->xo_attrs.xb_curp != xop->xo_attrs.xb_bufp) {
	    rc += xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp;
	    xo_data_append(xop, xop->xo_attrs.xb_bufp,
			   xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp);
	    xo_buf_reset(&xop->xo_attrs);
	}

	rc += xo_printf(xop, ">%s", ppn);
	break;

    case XO_STYLE_JSON:
	/*
	 * If we are newly "full", then we need all our parents to be emitted
	 */
	xo_filt_handle_change_status(xop, old_fstatus, fstatus);

	xo_stack_set_flags(xop);

	if (!XOF_ISSET(xop, XOF_NO_TOP)
	        && !XOIF_ISSET(xop, XOIF_TOP_EMITTED))
	    xo_emit_top(xop, ppn);

	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	/* If we need underscores, make a local copy and doctor it */
	const char *new_name = name;
	if (XOF_ISSET(xop, XOF_UNDERSCORES)) {
	    size_t len = strlen(name);
	    const char *old_name = name;
	    char *buf, *cp, *ep;

	    buf = alloca(len + 1);
	    for (cp = buf, ep = buf + len + 1; cp < ep; cp++, old_name++)
		*cp = (*old_name == '-') ? '_' : *old_name;
	    new_name = buf;
	}

	rc = xo_printf(xop, "%s%*s\"%s\": {%s",
		       pre_nl, xo_indent(xop), "", new_name, ppn);
	break;

    case XO_STYLE_SDPARAMS:
	break;

    case XO_STYLE_ENCODER:
	rc = xo_encoder_handle(xop, XO_OP_OPEN_CONTAINER, NULL,
			       name, NULL, flags);
	break;
    }

    /*
     * FULL frames are permanently committed; xs_rb_off must be CLEAR so the
     * close path writes the closing tag normally instead of entering the
     * rollback branch and skipping it.
     */
    if (XOF_ISSET(xop, XOF_FILTER)) {
	if (fstatus == XO_STATUS_FULL || fstatus == XO_STATUS_ZERO)
	    starting_offset = XS_OFFSET_CLEAR;
    }

    /*
     * After compact-commit clears XOIF_FILTERING, a new TRACK container
     * that opens as a sibling of the committed region still needs
     * XOIF_FILTERING set so that the buffer is not flushed to the fd
     * before the rollback on close (which would make the opening tag
     * permanent while the closing tag is suppressed).  Mirror the same
     * guard used in xo_do_open_instance().
     */
    if (XOF_ISSET(xop, XOF_FILTER)) {
	if (fstatus == XO_STATUS_PRED)
	    XOIF_SET(xop, XOIF_FILTERING);
	else if (fstatus == XO_STATUS_TRACK && old_fstatus == XO_STATUS_FULL)
	    XOIF_SET(xop, XOIF_FILTERING);
    }

    xo_depth_change(xop, name, 1, 1, XSS_OPEN_CONTAINER,
		    xo_stack_flags(flags), fstatus, starting_offset);

    xo_stack_cur(xop)->xs_tag_end = xo_buf_offset(&xop->xo_data);

    return rc;
}

xo_ssize_t
xo_open_container_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    return xo_transition(xop, flags, name, XSS_OPEN_CONTAINER);
}

xo_ssize_t
xo_open_container_h (xo_handle_t *xop, const char *name)
{
    return xo_open_container_hf(xop, 0, name);
}

xo_ssize_t
xo_open_container (const char *name)
{
    return xo_open_container_hf(NULL, 0, name);
}

xo_ssize_t
xo_open_container_hd (xo_handle_t *xop, const char *name)
{
    return xo_open_container_hf(xop, XOF_DTRT, name);
}

xo_ssize_t
xo_open_container_d (const char *name)
{
    return xo_open_container_hf(NULL, XOF_DTRT, name);
}

static int
xo_do_close_container (xo_handle_t *xop, const char *name)
{
    xop = xo_default(xop);

    ssize_t rc = 0;
    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    if (name == NULL) {
	xo_stack_t *xsp = xo_stack_cur(xop);

	name = xsp->xs_name;
	if (name) {
	    ssize_t len = strlen(name) + 1;
	    /* We need to make a local copy; xo_depth_change will free it */
	    char *cp = alloca(len);
	    memcpy(cp, name, len);
	    name = cp;
	} else {
	    if (!(xsp->xs_flags & XSF_DTRT))
		xo_failure(xop, "missing name without 'dtrt' mode");
	    name = XO_FAILURE_NAME;
	}
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    const char *leader = xo_xml_leader(xop, name);

    /* Now that the work is done, let the filtering code know */
    xo_stack_t *xsp = xo_stack_cur(xop);
    xo_filter_status_t old_fstatus = xsp->xs_fstatus;

    xo_filter_status_t fstatus;
    fstatus = xo_filter_close_container(xop, xo_filters(xop), name);

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	/*
	 * Roll back if this container was tentatively captured (xs_rb_off set)
	 * and is the top of the tentative region (parent's xs_rb_off is clear).
	 * Nested containers inside a still-tracked instance must not be rolled
	 * back independently — the enclosing instance handles that on close.
	 */
	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR) {
	    xo_filt_rollback(xop, xsp, old_fstatus, fstatus);
	    xo_depth_change(xop, name, -1, -1, XSS_CLOSE_CONTAINER,
			    XSF_FILTER, fstatus, 0);
	    break;
	}
	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_CONTAINER,
			XSF_FILTER, fstatus, 0);
	rc = xo_printf(xop, "%*s</%s%s>%s", xo_indent(xop),
		       "", leader, name, ppn);
	break;

    case XO_STYLE_JSON:
	xo_stack_set_flags(xop);

	pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
	ppn = "";

	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR) {
	    xo_filt_rollback(xop, xsp, old_fstatus, fstatus);
	    xo_depth_change(xop, name, -1, -1, XSS_CLOSE_CONTAINER, 0, 0, 0);
	    break;
	}
	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_CONTAINER, 0, 0, 0);
	rc = xo_printf(xop, "%s%*s}%s", pre_nl, xo_indent(xop), "", ppn);
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_CONTAINER, 0, 0, 0);
	break;

    case XO_STYLE_SDPARAMS:
	break;

    case XO_STYLE_ENCODER:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_CONTAINER, 0, 0, 0);
	rc = xo_encoder_handle(xop, XO_OP_CLOSE_CONTAINER, NULL, name, NULL, 0);
	break;
    }

    return rc;
}

xo_ssize_t
xo_close_container_h (xo_handle_t *xop, const char *name)
{
    return xo_transition(xop, 0, name, XSS_CLOSE_CONTAINER);
}

xo_ssize_t
xo_close_container (const char *name)
{
    return xo_close_container_h(NULL, name);
}

xo_ssize_t
xo_close_container_hd (xo_handle_t *xop)
{
    return xo_close_container_h(xop, NULL);
}

xo_ssize_t
xo_close_container_d (void)
{
    return xo_close_container_h(NULL, NULL);
}

static int
xo_do_open_list (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    ssize_t rc = 0;
    int indent = 0;

    xop = xo_default(xop);

    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_off_t starting_offset = xo_buf_offset(&xop->xo_data);
    xop->xo_rb_snap = xop->xo_stack[xop->xo_depth].xs_flags & XSF_RB_BITS;

    switch (xo_style(xop)) {
    case XO_STYLE_JSON:

	indent = 1;
	if (!XOF_ISSET(xop, XOF_NO_TOP)
		&& !XOIF_ISSET(xop, XOIF_TOP_EMITTED))
	    xo_emit_top(xop, ppn);

	if (name == NULL) {
	    xo_failure(xop, "NULL passed for list name");
	    name = XO_FAILURE_NAME;
	}

	xo_stack_set_flags(xop);

	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	/* If we need underscores, make a local copy and doctor it */
	const char *new_name = name;
	if (XOF_ISSET(xop, XOF_UNDERSCORES)) {
	    size_t len = strlen(name);
	    const char *old_name = name;
	    char *buf, *cp, *ep;

	    buf = alloca(len + 1);
	    for (cp = buf, ep = buf + len + 1; cp < ep; cp++, old_name++)
		*cp = (*old_name == '-') ? '_' : *old_name;
	    new_name = buf;
	}

	rc = xo_printf(xop, "%s%*s\"%s\": [%s",
		       pre_nl, xo_indent(xop), "", new_name, ppn);
	break;

    case XO_STYLE_ENCODER:
	rc = xo_encoder_handle(xop, XO_OP_OPEN_LIST, NULL, name, NULL, flags);
	break;
    }

    /*
     * When the parent frame is committed (FULL), its xs_rb_off is CLEAR but
     * the list frame we are about to push will carry a live starting_offset.
     * If a tty or high-water flush fires before the first FULL item triggers
     * commit_compact, that offset becomes meaningless — commit_compact would
     * use it to set xb_curp to a position that no longer corresponds to the
     * ancestor content in the buffer, corrupting output.  Keep
     * XOIF_FILTERING set so xo_avoid_flushing() suppresses any such flush
     * until commit_compact fires and clears both xs_rb_off and XOIF_FILTERING.
     */
    if (XOF_ISSET(xop, XOF_FILTER)) {
	if (xo_stack_cur(xop)->xs_fstatus == XO_STATUS_FULL)
	    XOIF_SET(xop, XOIF_FILTERING);
    }

    xo_depth_change(xop, name, 1, indent, XSS_OPEN_LIST,
		    XSF_LIST | xo_stack_flags(flags), 0, starting_offset);

    xo_stack_cur(xop)->xs_tag_end = xo_buf_offset(&xop->xo_data);

    return rc;
}

xo_ssize_t
xo_open_list_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    return xo_transition(xop, flags, name, XSS_OPEN_LIST);
}

xo_ssize_t
xo_open_list_h (xo_handle_t *xop, const char *name)
{
    return xo_open_list_hf(xop, 0, name);
}

xo_ssize_t
xo_open_list (const char *name)
{
    return xo_open_list_hf(NULL, 0, name);
}

xo_ssize_t
xo_open_list_hd (xo_handle_t *xop, const char *name)
{
    return xo_open_list_hf(xop, XOF_DTRT, name);
}

xo_ssize_t
xo_open_list_d (const char *name)
{
    return xo_open_list_hf(NULL, XOF_DTRT, name);
}

static int
xo_do_close_list (xo_handle_t *xop, const char *name)
{
    ssize_t rc = 0;
    const char *pre_nl = "";

    if (name == NULL) {
	xo_stack_t *xsp = xo_stack_cur(xop);

	name = xsp->xs_name;
	if (name) {
	    ssize_t len = strlen(name) + 1;
	    /* We need to make a local copy; xo_depth_change will free it */
	    char *cp = alloca(len);
	    memcpy(cp, name, len);
	    name = cp;
	} else {
	    if (!(xsp->xs_flags & XSF_DTRT))
		xo_failure(xop, "missing name without 'dtrt' mode");
	    name = XO_FAILURE_NAME;
	}
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_stack_t *xsp = xo_stack_cur(xop);

    switch (xo_style(xop)) {
    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR) {
	    xo_filt_rollback(xop, xsp, xsp->xs_fstatus, xsp->xs_fstatus);
	    xo_depth_change(xop, name, -1, -1, XSS_CLOSE_LIST, XSF_LIST, 0, 0);
	    break;
	}

	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_LIST, XSF_LIST, 0, 0);
	rc = xo_printf(xop, "%s%*s]", pre_nl, xo_indent(xop), "");
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_ENCODER:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_LIST, XSF_LIST, 0, 0);
	rc = xo_encoder_handle(xop, XO_OP_CLOSE_LIST, NULL, name, NULL, 0);
	break;

    default:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_LIST, XSF_LIST, 0, 0);
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;
    }

    return rc;
}

xo_ssize_t
xo_close_list_h (xo_handle_t *xop, const char *name)
{
    return xo_transition(xop, 0, name, XSS_CLOSE_LIST);
}

xo_ssize_t
xo_close_list (const char *name)
{
    return xo_close_list_h(NULL, name);
}

xo_ssize_t
xo_close_list_hd (xo_handle_t *xop)
{
    return xo_close_list_h(xop, NULL);
}

xo_ssize_t
xo_close_list_d (void)
{
    return xo_close_list_h(NULL, NULL);
}

static int
xo_do_open_leaf_list (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    ssize_t rc = 0;
    int indent = 0;

    xop = xo_default(xop);

    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_off_t starting_offset = xo_buf_offset(&xop->xo_data);
    xop->xo_rb_snap = xop->xo_stack[xop->xo_depth].xs_flags & XSF_RB_BITS;

    switch (xo_style(xop)) {
    case XO_STYLE_JSON:
	indent = 1;

	if (!XOF_ISSET(xop, XOF_NO_TOP)) {
	    if (!XOIF_ISSET(xop, XOIF_TOP_EMITTED)) {
		xo_printf(xop, "%*s{%s", xo_indent(xop), "", ppn);
		XOIF_SET(xop, XOIF_TOP_EMITTED);
	    }
	}

	if (name == NULL) {
	    xo_failure(xop, "NULL passed for list name");
	    name = XO_FAILURE_NAME;
	}

	xo_stack_set_flags(xop);

	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	rc = xo_printf(xop, "%s%*s\"%s\": [%s",
		       pre_nl, xo_indent(xop), "", name, ppn);
	break;

    case XO_STYLE_ENCODER:
	rc = xo_encoder_handle(xop, XO_OP_OPEN_LEAF_LIST, NULL,
			       name, NULL, flags);
	break;
    }

    xo_depth_change(xop, name, 1, indent, XSS_OPEN_LEAF_LIST,
		    XSF_LIST | xo_stack_flags(flags), 0, starting_offset);

    xo_stack_cur(xop)->xs_tag_end = xo_buf_offset(&xop->xo_data);

    return rc;
}

static int
xo_do_close_leaf_list (xo_handle_t *xop, const char *name)
{
    ssize_t rc = 0;
    const char *pre_nl = "";

    if (name == NULL) {
	xo_stack_t *xsp = xo_stack_cur(xop);

	name = xsp->xs_name;
	if (name) {
	    ssize_t len = strlen(name) + 1;
	    /* We need to make a local copy; xo_depth_change will free it */
	    char *cp = alloca(len);
	    memcpy(cp, name, len);
	    name = cp;
	} else {
	    if (!(xsp->xs_flags & XSF_DTRT))
		xo_failure(xop, "missing name without 'dtrt' mode");
	    name = XO_FAILURE_NAME;
	}
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_stack_t *xsp = xo_stack_cur(xop);

    switch (xo_style(xop)) {
    case XO_STYLE_JSON:
	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR) {
	    xo_filt_rollback(xop, xsp, xsp->xs_fstatus, xsp->xs_fstatus);
	    xo_depth_change(xop, name, -1, -1, XSS_CLOSE_LEAF_LIST, XSF_LIST, 0, 0);
	    break;
	}

	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_LEAF_LIST, XSF_LIST, 0, 0);
	rc = xo_printf(xop, "%s%*s]", pre_nl, xo_indent(xop), "");
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_XML:
	/*
	 * A leaf-list emits no wrapping tag, but its members may have been
	 * buffered tentatively while filtering (xs_rb_off set).  Roll them
	 * back on close if the list never matched, just as the JSON path
	 * (and xo_do_close_container/instance) do.
	 */
	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR)
	    xo_filt_rollback(xop, xsp, xsp->xs_fstatus, xsp->xs_fstatus);
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_LEAF_LIST, XSF_LIST, 0, 0);
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_ENCODER:
	rc = xo_encoder_handle(xop, XO_OP_CLOSE_LEAF_LIST, NULL, name, NULL, 0);
	/* FALLTHRU */

    default:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_LEAF_LIST, XSF_LIST, 0, 0);
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;
    }

    return rc;
}

static int
xo_do_open_instance (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    xop = xo_default(xop);

    ssize_t rc = 0;
    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    if (name == NULL) {
	xo_failure(xop, "NULL passed for instance name");
	name = XO_FAILURE_NAME;
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    xo_stack_t *xsp = xo_stack_cur(xop);
    xo_filter_status_t old_fstatus = xsp->xs_fstatus;

    ssize_t start_offset = xo_buf_offset(&xop->xo_data);
    xop->xo_rb_snap = xop->xo_stack[xop->xo_depth].xs_flags & XSF_RB_BITS;

    xo_filter_status_t fstatus;
    fstatus = xo_filter_open_instance(xop, xo_filters(xop), name);

    const char *leader = xo_xml_leader(xop, name);
    flags |= xop->xo_flags;

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	if (fstatus == XO_STATUS_DEAD) /* No one wants this */
	    break;

	/*
	 * If we are newly "full", then we need all our parents to be emitted
	 */
	xo_filt_handle_change_status(xop, old_fstatus, fstatus);

	rc = xo_printf(xop, "%*s<%s%s", xo_indent(xop), "", leader, name);

	if (xop->xo_attrs.xb_curp != xop->xo_attrs.xb_bufp) {
	    rc += xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp;
	    xo_data_append(xop, xop->xo_attrs.xb_bufp,
			   xop->xo_attrs.xb_curp - xop->xo_attrs.xb_bufp);
	    xo_buf_reset(&xop->xo_attrs);
	}

	rc += xo_printf(xop, ">%s", ppn);
	break;

    case XO_STYLE_JSON:
	/*
	 * If we are newly "full", then we need all our parents to be emitted
	 */
	xo_filt_handle_change_status(xop, old_fstatus, fstatus);

	xo_stack_set_flags(xop);

	if (xop->xo_stack[xop->xo_depth].xs_flags & XSF_NOT_FIRST)
	    pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? ",\n" : ", ";
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;

	rc = xo_printf(xop, "%s%*s{%s",
		       pre_nl, xo_indent(xop), "", ppn);
	break;

    case XO_STYLE_SDPARAMS:
	break;

    case XO_STYLE_ENCODER:
	rc = xo_encoder_handle(xop, XO_OP_OPEN_INSTANCE, NULL,
			       name, NULL, flags);
	break;
    }

    /*
     * Set XOIF_FILTERING so xs_rb_off is preserved by xo_depth_change:
     *
     * PRED: key/non-key predicate pending — buffer tentatively until resolved.
     *
     * TRACK after FULL: positional predicate already matched (old_fstatus==FULL);
     *   subsequent siblings are TRACK and must be buffered so their whiteboard
     *   can be discarded on close.
     */
    if (XOF_ISSET(xop, XOF_FILTER)) {
	if (fstatus == XO_STATUS_PRED)
	    XOIF_SET(xop, XOIF_FILTERING);
	else if (fstatus == XO_STATUS_TRACK && old_fstatus == XO_STATUS_FULL)
	    XOIF_SET(xop, XOIF_FILTERING);
    }

    xo_depth_change(xop, name, 1, 1, XSS_OPEN_INSTANCE,
		    xo_stack_flags(flags), fstatus, start_offset);

    xo_stack_cur(xop)->xs_tag_end = xo_buf_offset(&xop->xo_data);

    return rc;
}

xo_ssize_t
xo_open_instance_hf (xo_handle_t *xop, xo_xof_flags_t flags, const char *name)
{
    return xo_transition(xop, flags, name, XSS_OPEN_INSTANCE);
}

xo_ssize_t
xo_open_instance_h (xo_handle_t *xop, const char *name)
{
    return xo_open_instance_hf(xop, 0, name);
}

xo_ssize_t
xo_open_instance (const char *name)
{
    return xo_open_instance_hf(NULL, 0, name);
}

xo_ssize_t
xo_open_instance_hd (xo_handle_t *xop, const char *name)
{
    return xo_open_instance_hf(xop, XOF_DTRT, name);
}

xo_ssize_t
xo_open_instance_d (const char *name)
{
    return xo_open_instance_hf(NULL, XOF_DTRT, name);
}

static int
xo_do_close_instance (xo_handle_t *xop, const char *name)
{
    xop = xo_default(xop);

    ssize_t rc = 0;
    const char *ppn = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";
    const char *pre_nl = "";

    if (name == NULL) {
	xo_stack_t *xsp = xo_stack_cur(xop);

	name = xsp->xs_name;
	if (name) {
	    ssize_t len = strlen(name) + 1;
	    /* We need to make a local copy; xo_depth_change will free it */
	    char *cp = alloca(len);
	    memcpy(cp, name, len);
	    name = cp;
	} else {
	    if (!(xsp->xs_flags & XSF_DTRT))
		xo_failure(xop, "missing name without 'dtrt' mode");
	    name = XO_FAILURE_NAME;
	}
    }

    name = xo_map_name(xop, name); /* Find mapped name, if any */

    const char *leader = xo_xml_leader(xop, name);

    xo_stack_t *xsp = xo_stack_cur(xop);
    xo_filter_status_t old_fstatus = xsp->xs_fstatus;

    /* Let the filter code know we're closing */
    xo_filter_status_t fstatus;
    fstatus = xo_filter_close_instance(xop, xo_filters(xop), name);

    /*
     * If a non-key predicate was force-resolved to FULL at close time
     * (the predicate field was absent from this instance), commit the
     * rollback so all tentatively-buffered sibling content is kept.
     */
    if (XOF_ISSET(xop, XOF_FILTER)
	    && old_fstatus == XO_STATUS_PRED && fstatus == XO_STATUS_FULL) {
	xo_filt_commit(xop, xsp, XO_STATUS_FULL);
	old_fstatus = XO_STATUS_FULL;
    }

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	if (XOF_ISSET(xop, XOF_FILTER)) {
	    xo_filt_rollback(xop, xsp, old_fstatus, fstatus);
	}

	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_INSTANCE, 0, fstatus, 0);

	if (!XOF_ISSET(xop, XOF_FILTER)
	        || xo_filt_want_output(xop, old_fstatus))
	    rc = xo_printf(xop, "%*s</%s%s>%s", xo_indent(xop), "",
			   leader, name, ppn);
	break;

    case XO_STYLE_JSON:
	pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";

	if (XOF_ISSET(xop, XOF_FILTER) && xsp->xs_rb_off != XS_OFFSET_CLEAR) {
	    xo_filt_rollback(xop, xsp, old_fstatus, fstatus);
	    xo_depth_change(xop, name, -1, -1, XSS_CLOSE_INSTANCE, 0, 0, 0);
	    break;
	}

	xo_depth_change(xop, name, -1, -1, XSS_CLOSE_INSTANCE, 0, 0, 0);
	rc = xo_printf(xop, "%s%*s}", pre_nl, xo_indent(xop), "");
	xop->xo_stack[xop->xo_depth].xs_flags |= XSF_NOT_FIRST;
	break;

    case XO_STYLE_HTML:
    case XO_STYLE_TEXT:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_INSTANCE, 0, 0, 0);
	break;

    case XO_STYLE_SDPARAMS:
	break;

    case XO_STYLE_ENCODER:
	xo_depth_change(xop, name, -1, 0, XSS_CLOSE_INSTANCE, 0, 0, 0);
	rc = xo_encoder_handle(xop, XO_OP_CLOSE_INSTANCE, NULL, name, NULL, 0);
	break;
    }

    return rc;
}

xo_ssize_t
xo_close_instance_h (xo_handle_t *xop, const char *name)
{
    return xo_transition(xop, 0, name, XSS_CLOSE_INSTANCE);
}

xo_ssize_t
xo_close_instance (const char *name)
{
    return xo_close_instance_h(NULL, name);
}

xo_ssize_t
xo_close_instance_hd (xo_handle_t *xop)
{
    return xo_close_instance_h(xop, NULL);
}

xo_ssize_t
xo_close_instance_d (void)
{
    return xo_close_instance_h(NULL, NULL);
}

static int
xo_do_close_all (xo_handle_t *xop, xo_stack_t *limit)
{
    xo_stack_t *xsp;
    ssize_t rc = 0;
    xo_xsf_flags_t flags;

    for (xsp = xo_stack_cur(xop); xsp >= limit; xsp--) {
	switch (xsp->xs_state) {
	case XSS_INIT:
	    /* Nothing */
	    rc = 0;
	    break;

	case XSS_OPEN_CONTAINER:
	    rc = xo_do_close_container(xop, NULL);
	    break;

	case XSS_OPEN_LIST:
	    rc = xo_do_close_list(xop, NULL);
	    break;

	case XSS_OPEN_INSTANCE:
	    rc = xo_do_close_instance(xop, NULL);
	    break;

	case XSS_OPEN_LEAF_LIST:
	    rc = xo_do_close_leaf_list(xop, NULL);
	    break;

	case XSS_MARKER:
	    flags = xsp->xs_flags & XSF_MARKER_FLAGS;
	    xo_depth_change(xop, xsp->xs_name, -1, 0, XSS_MARKER, 0, 0, 0);
	    xop->xo_stack[xop->xo_depth].xs_flags |= flags;
	    rc = 0;
	    break;
	}

	if (rc < 0)
	    xo_failure(xop, "close %d failed: %d", xsp->xs_state, rc);
    }

    return 0;
}

/*
 * This function is responsible for clearing out whatever is needed
 * to get to the desired state, if possible.
 */
static int
xo_do_close (xo_handle_t *xop, const char *name, xo_state_t new_state)
{
    xo_stack_t *xsp, *limit = NULL;
    ssize_t rc;
    xo_state_t need_state = new_state;

    if (new_state == XSS_CLOSE_CONTAINER)
	need_state = XSS_OPEN_CONTAINER;
    else if (new_state == XSS_CLOSE_LIST)
	need_state = XSS_OPEN_LIST;
    else if (new_state == XSS_CLOSE_INSTANCE)
	need_state = XSS_OPEN_INSTANCE;
    else if (new_state == XSS_CLOSE_LEAF_LIST)
	need_state = XSS_OPEN_LEAF_LIST;
    else if (new_state == XSS_MARKER)
	need_state = XSS_MARKER;
    else
	return 0; /* Unknown or useless new states are ignored */

    name = xo_map_name(xop, name);

    for (xsp = xo_stack_cur(xop); xsp > xop->xo_stack; xsp--) {
	/*
	 * Marker's normally stop us from going any further, unless
	 * we are popping a marker (new_state == XSS_MARKER).
	 */
	if (xsp->xs_state == XSS_MARKER && need_state != XSS_MARKER) {
	    if (name) {
		xo_failure(xop, "close (xo_%s) fails at marker '%s'; "
			   "not found '%s'",
			   xo_state_name(new_state),
			   xsp->xs_name, name);
		return 0;

	    } else {
		limit = xsp;
		xo_failure(xop, "close stops at marker '%s'", xsp->xs_name);
	    }
	    break;
	}
	
	if (xsp->xs_state != need_state)
	    continue;

	if (name && xsp->xs_name && !xo_streq(name, xsp->xs_name))
	    continue;

	limit = xsp;
	break;
    }

    if (limit == NULL) {
	xo_failure(xop, "xo_%s can't find match for '%s'",
		   xo_state_name(new_state), name);
	return 0;
    }

    rc = xo_do_close_all(xop, limit);

    return rc;
}

/*
 * We are in a given state and need to transition to the new state.
 */
static ssize_t
xo_marker_prevents_close (xo_handle_t *xop, int old_state, int new_state)
{
    xo_failure(xop, "marker '%s' prevents transition from %s to %s",
	       xop->xo_stack[xop->xo_depth].xs_name,
	       xo_state_name(old_state), xo_state_name(new_state));
    return -1;
}

static ssize_t
xo_transition (xo_handle_t *xop, xo_xof_flags_t flags, const char *name,
	       xo_state_t new_state)
{
    ssize_t rc = 0;

    xop = xo_default(xop);

    xo_stack_t *xsp = xo_stack_cur(xop);
    int old_state = xsp->xs_state;
    int on_marker = (old_state == XSS_MARKER);
    int flush = XOF_ISSET(xop, XOF_FLUSH);

    /* If there's a marker on top of the stack, we need to find a real state */
    while (old_state == XSS_MARKER) {
	if (xsp == xop->xo_stack)
	    break;
	xsp -= 1;
	old_state = xsp->xs_state;
    }

    /*
     * At this point, the list of possible states are:
     *   XSS_INIT, XSS_OPEN_CONTAINER, XSS_OPEN_LIST,
     *   XSS_OPEN_INSTANCE, XSS_OPEN_LEAF_LIST, XSS_DISCARDING
     */
    switch (XSS_TRANSITION(old_state, new_state)) {

    case XSS_TRANSITION(XSS_INIT, XSS_OPEN_CONTAINER):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_OPEN_CONTAINER):
    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_OPEN_CONTAINER):
       rc = xo_do_open_container(xop, flags, name);
       break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_OPEN_CONTAINER):
    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_OPEN_CONTAINER):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_open_container(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_CLOSE_CONTAINER):
	/* This is an exception for "xo --close" */
	rc = xo_do_close_container(xop, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_CLOSE_CONTAINER):
    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_CLOSE_CONTAINER):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_CLOSE_CONTAINER):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_CLOSE_CONTAINER):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_OPEN_LIST):
    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_OPEN_LIST):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_OPEN_LIST):
	rc = xo_do_open_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_OPEN_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_open_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_OPEN_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_open_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_CLOSE_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_CLOSE_LIST):
    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_CLOSE_LIST):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_CLOSE_LIST):
    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_CLOSE_LIST):
	rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_OPEN_INSTANCE):
	rc = xo_do_open_instance(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_OPEN_INSTANCE):
    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_OPEN_INSTANCE):
	rc = xo_do_open_list(xop, flags, name);
	if (rc >= 0)
	    rc = xo_do_open_instance(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_OPEN_INSTANCE):
	if (on_marker) {
	    rc = xo_do_open_list(xop, flags, name);
	} else {
	    rc = xo_do_close_instance(xop, NULL);
	}
	if (rc >= 0)
	    rc = xo_do_open_instance(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_OPEN_INSTANCE):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_open_instance(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_CLOSE_INSTANCE):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_instance(xop, name);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_CLOSE_INSTANCE):
	/* This one makes no sense; ignore it */
	xo_failure(xop, "xo_close_instance ignored when called from "
		   "initial state ('%s')", name ?: "(unknown)");
	break;

    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_CLOSE_INSTANCE):
    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_CLOSE_INSTANCE):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_CLOSE_INSTANCE):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_OPEN_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_OPEN_LEAF_LIST):
    case XSS_TRANSITION(XSS_INIT, XSS_OPEN_LEAF_LIST):
	rc = xo_do_open_leaf_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_OPEN_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_OPEN_LEAF_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_list(xop, NULL);
	if (rc >= 0)
	    rc = xo_do_open_leaf_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_CLOSE_LEAF_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, name);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_CLOSE_LEAF_LIST):
	/* Makes no sense; ignore */
	xo_failure(xop, "xo_close_leaf_list ignored when called from "
		   "initial state ('%s')", name ?: "(unknown)");
	break;

    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_CLOSE_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_CLOSE_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_CLOSE_LEAF_LIST):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close(xop, name, new_state);
	break;

    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_EMIT):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_EMIT):
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_EMIT):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close(xop, NULL, XSS_CLOSE_LIST);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_EMIT):
	/* Missing a top-level container, so we must fake one */
	if (!XOF_ISSET(xop, XOF_NO_TOP_LEVEL)) {
	    xo_failure(xop, "emitting a field before a top-level tag");
	    rc = xo_do_open_container(xop, flags, XO_FAKE_TOP_LEVEL_NAME);
	}
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_EMIT):
	if (on_marker)
	    return xo_marker_prevents_close(xop, old_state, new_state);
	rc = xo_do_close_leaf_list(xop, NULL);
	break;

    case XSS_TRANSITION(XSS_INIT, XSS_EMIT_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_CONTAINER, XSS_EMIT_LEAF_LIST):
    case XSS_TRANSITION(XSS_OPEN_INSTANCE, XSS_EMIT_LEAF_LIST):
	rc = xo_do_open_leaf_list(xop, flags, name);
	break;

    case XSS_TRANSITION(XSS_OPEN_LEAF_LIST, XSS_EMIT_LEAF_LIST):
	break;

    case XSS_TRANSITION(XSS_OPEN_LIST, XSS_EMIT_LEAF_LIST):
	/*
	 * We need to be backward compatible with the pre-xo_open_leaf_list
	 * API, where both lists and leaf-lists were opened as lists.  So
	 * if we find an open list that hasn't had anything written to it,
	 * we'll accept it.
	 */
	break;

    default:
	xo_failure(xop, "unknown transition: (%u -> %u)",
		   xsp->xs_state, new_state);
    }

    /*
     * If we've got enough data, flush it.
     */
    if (xo_buf_offset(&xop->xo_data) > XO_BUF_HIGH_WATER)
	flush = 1;

    /* Handle the flush flag */
    if (flush && rc >= 0 && !xo_avoid_flushing(xop))
	if (xo_flush_h(xop) < 0)
	    rc = -1;

    return rc;
}

xo_ssize_t
xo_open_marker_h (xo_handle_t *xop, const char *name)
{
    xop = xo_default(xop);

    xo_depth_change(xop, name, 1, 0, XSS_MARKER,
	    xop->xo_stack[xop->xo_depth].xs_flags & XSF_MARKER_FLAGS, 0, 0);

    return 0;
}

xo_ssize_t
xo_open_marker (const char *name)
{
    return xo_open_marker_h(NULL, name);
}

xo_ssize_t
xo_close_marker_h (xo_handle_t *xop, const char *name)
{
    xop = xo_default(xop);

    return xo_do_close(xop, name, XSS_MARKER);
}

xo_ssize_t
xo_close_marker (const char *name)
{
    return xo_close_marker_h(NULL, name);
}

/*
 * Record custom output functions into the xo handle, allowing
 * integration with a variety of output frameworks.
 */
void
xo_set_writer (xo_handle_t *xop, void *opaque, xo_write_func_t write_func,
	       xo_close_func_t close_func, xo_flush_func_t flush_func)
{
    xop = xo_default(xop);

    xop->xo_opaque = opaque;
    xop->xo_write = write_func;
    xop->xo_close = close_func;
    xop->xo_flush = flush_func;
}

void
xo_set_allocator (xo_realloc_func_t realloc_func, xo_free_func_t free_func)
{
    xo_realloc = realloc_func;
    xo_free = free_func;
}

xo_ssize_t
xo_flush_h (xo_handle_t *xop)
{
    ssize_t rc;

    xop = xo_default(xop);

    switch (xo_style(xop)) {
    case XO_STYLE_ENCODER:
	xo_encoder_handle(xop, XO_OP_FLUSH, NULL, NULL, NULL, 0);
    }

    rc = xo_write(xop);
    if (rc >= 0 && xop->xo_flush)
	if (xop->xo_flush(xop->xo_opaque) < 0)
	    return -1;

    return rc;
}

xo_ssize_t
xo_flush (void)
{
    return xo_flush_h(NULL);
}

xo_ssize_t
xo_finish_h (xo_handle_t *xop)
{
    const char *open_if_empty = "";
    xop = xo_default(xop);

    if (!XOF_ISSET(xop, XOF_NO_CLOSE))
	xo_do_close_all(xop, xop->xo_stack);

    switch (xo_style(xop)) {
    case XO_STYLE_JSON:
	if (!XOF_ISSET(xop, XOF_NO_TOP)) {
	    const char *pre_nl = XOF_ISSET(xop, XOF_PRETTY) ? "\n" : "";

	    if (XOIF_ISSET(xop, XOIF_TOP_EMITTED))
		XOIF_CLEAR(xop, XOIF_TOP_EMITTED); /* Turn off before output */
	    else if (!(XOIF_ISSET(xop, XOIF_MADE_OUTPUT)
		       || !xo_buf_is_empty(&xop->xo_data))) {
		/*
		 * No output made and nothing pending in the buffer,
		 * so have fake up an empty set of braces just to make
		 * valid JSON.
		 */
		open_if_empty = "{ ";
		pre_nl = "";
	    }

	    xo_printf(xop, "%s%*s%s}\n",
		      pre_nl, xo_indent(xop), "", open_if_empty);
	}
	break;

    case XO_STYLE_ENCODER:
	xo_encoder_handle(xop, XO_OP_FINISH, NULL, NULL, NULL, 0);
	break;
    }

    return xo_flush_h(xop);
}

xo_ssize_t
xo_finish (void)
{
    return xo_finish_h(NULL);
}

/*
 * xo_finish_atexit is suitable for atexit() calls, to force clear up
 * and finalizing output.
 */
void
xo_finish_atexit (void)
{
    (void) xo_finish_h(NULL);
}

/*
 * Generate an error message, such as would be displayed on stderr
 */
void
xo_errorn_hv (xo_handle_t *xop, int need_newline, const char *fmt, va_list vap)
{
    xop = xo_default(xop);

    /*
     * If the format string doesn't end with a newline, we pop
     * one on ourselves.
     */
    if (need_newline) {
	ssize_t len = strlen(fmt);
	if (len > 0 && fmt[len - 1] != '\n') {
	    char *newfmt = alloca(len + 2);
	    memcpy(newfmt, fmt, len);
	    newfmt[len] = '\n';
	    newfmt[len + 1] = '\0';
	    fmt = newfmt;
	}
    }

    switch (xo_style(xop)) {
    case XO_STYLE_TEXT:
	vfprintf(stderr, fmt, vap);
	break;

    case XO_STYLE_HTML:
	va_copy(xop->xo_vap, vap);
	
	xo_buf_append_div(xop, "error", 0, NULL, 0, NULL, 0,
			  fmt, strlen(fmt), NULL, 0);

	if (XOIF_ISSET(xop, XOIF_DIV_OPEN))
	    xo_line_close(xop);

	xo_write(xop);

	va_end(xop->xo_vap);
	bzero(&xop->xo_vap, sizeof(xop->xo_vap));
	break;

    case XO_STYLE_XML:
    case XO_STYLE_JSON:
	va_copy(xop->xo_vap, vap);

	xo_open_container_h(xop, "error");
	xo_format_value(xop, "message", 7, NULL, 0,
			fmt, strlen(fmt), NULL, 0, 0);
	xo_close_container_h(xop, "error");

	va_end(xop->xo_vap);
	bzero(&xop->xo_vap, sizeof(xop->xo_vap));
	break;

    case XO_STYLE_SDPARAMS:
    case XO_STYLE_ENCODER:
	break;
    }
}

void
xo_error_h (xo_handle_t *xop, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_errorn_hv(xop, 0, fmt, vap);
    va_end(vap);
}

/*
 * Generate an error message, such as would be displayed on stderr
 */
void
xo_error (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_errorn_hv(NULL, 0, fmt, vap);
    va_end(vap);
}

void
xo_errorn_h (xo_handle_t *xop, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_errorn_hv(xop, 1, fmt, vap);
    va_end(vap);
}

/*
 * Generate an error message, such as would be displayed on stderr
 */
void
xo_errorn (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_errorn_hv(NULL, 1, fmt, vap);
    va_end(vap);
}

/*
 * Parse any libxo-specific options from the command line, removing them
 * so the main() argument parsing won't see them.  We return the new value
 * for argc or -1 for error.  If an error occurred, the program should
 * exit.  A suitable error message has already been displayed.
 */
int
xo_parse_args (int argc, char **argv)
{
    static char libxo_opt[] = "--libxo";
    char *cp;
    int i, save;

    /*
     * If xo_set_program has always been called, we honor that value
     */
    if (xo_program == NULL) {
	/* Save our program name for xo_err and friends */
	xo_program = argv[0];
	cp = strrchr(xo_program, '/');
	if (cp)
	    xo_program = ++cp;
	else
	    cp = argv[0];		/* Reset to front of string */

	/*
	 * GNU libtool add an annoying ".test" as the program
	 * extension; we remove it.  libtool also adds a "lt-" prefix
	 * that we cannot remove.
	 */
	size_t len = strlen(xo_program);
	static const char gnu_ext[] = ".test";
	if (len >= sizeof(gnu_ext)) {
	    cp += len + 1 - sizeof(gnu_ext);
	    if (xo_streq(cp, gnu_ext))
		*cp = '\0';
	}
    }

    xo_handle_t *xop = xo_default(NULL);

    for (save = i = 1; i < argc; i++) {
	if (argv[i] == NULL
	    || strncmp(argv[i], libxo_opt, sizeof(libxo_opt) - 1) != 0) {
	    if (save != i)
		argv[save] = argv[i];
	    save += 1;
	    continue;
	}

	cp = argv[i] + sizeof(libxo_opt) - 1;
	if (*cp == '\0') {
	    cp = argv[++i];
	    if (cp == NULL) {
		xo_warnx("missing libxo option");
		return -1;
	    }
		
	    if (xo_set_options(xop, cp) < 0)
		return -1;
	} else if (*cp == ':') {
	    if (xo_set_options(xop, cp) < 0)
		return -1;

	} else if (*cp == '=') {
	    if (xo_set_options(xop, ++cp) < 0)
		return -1;

	} else if (*cp == '-') {
	    cp += 1;
	    if (xo_streq(cp, "check")) {
		exit(XO_HAS_LIBXO);

	    } else {
		xo_warnx("unknown libxo option: '%s'", argv[i]);
		return -1;
	    }
	} else {
		xo_warnx("unknown libxo option: '%s'", argv[i]);
	    return -1;
	}
    }

    /*
     * We only want to do color output on terminals, but we only want
     * to do this if the user has asked for color.
     */
    if (XOF_ISSET(xop, XOF_COLOR_ALLOWED) && isatty(1))
	XOF_SET(xop, XOF_COLOR);

    argv[save] = NULL;
    return save;
}


/*
 * This diagnostic function is something I will ask you to call from
 * your program when you write to tell me libxo has gone bat-guano
 * crazy and has discarded your list or container or content.  Output
 * content will be what we lovingly call "developer entertainment".
 *
 * Debugging function that dumps the current stack of open libxo constructs,
 * suitable for calling from the debugger.
 *
 * @param[in] xop A valid libxo handle, or NULL for the default handle
 */
void
xo_dump_stack (xo_handle_t *xop);
void
xo_dump_stack (xo_handle_t *xop)
{
    int i;
    xo_stack_t *xsp;

    xop = xo_default(xop);

    fprintf(stderr, "Stack dump: (buf: cur %ld, size %ld)\n",
	    xop->xo_data.xb_curp - xop->xo_data.xb_bufp, xop->xo_data.xb_size);

    xsp = xop->xo_stack;
    for (i = 1, xsp++; i <= xop->xo_depth; i++, xsp++) {
	fprintf(stderr, "   [%d] %s '%s' [%x] rb_off: %ld\n",
		i, xo_state_name(xsp->xs_state),
		xsp->xs_name ?: "--", xsp->xs_flags, xsp->xs_rb_off);
    }
}

/*
 * Record the program name used for error messages
 */
void
xo_set_program (const char *name)
{
    xo_program = name;
}

void
xo_set_version_h (xo_handle_t *xop, const char *version)
{
    xop = xo_default(xop);

    if (version == NULL || strchr(version, '"') != NULL)
	return;

    if (!xo_style_is_encoding(xop))
	return;

    switch (xo_style(xop)) {
    case XO_STYLE_XML:
	/* For XML, we record this as an attribute for the first tag */
	xo_attr_h(xop, "version", "%s", version);
	break;

    case XO_STYLE_JSON:
	/*
	 * For JSON, we record the version string in our handle, and emit
	 * it in xo_emit_top.
	 */
	xop->xo_version = xo_strndup(version, -1);
	break;

    case XO_STYLE_ENCODER:
	xo_encoder_handle(xop, XO_OP_VERSION, NULL, NULL, version, 0);
	break;
    }
}

/*
 * Set the version number for the API content being carried through
 * the xo handle.
 */
void
xo_set_version (const char *version)
{
    xo_set_version_h(NULL, version);
}

/*
 * Generate a warning.  Normally, this is a text message written to
 * standard error.  If the XOF_WARN_XML flag is set, then we generate
 * XMLified content on standard output.
 */
void
xo_emit_warn_hcv (xo_handle_t *xop, int as_warning, int code,
		  const char *fmt, va_list vap)
{
    xop = xo_default(xop);

    if (fmt == NULL)
	return;

    xo_open_marker_h(xop, "xo_emit_warn_hcv");
    xo_open_container_h(xop, as_warning ? "__warning" : "__error");

    if (xo_program)
	xo_emit("{wc:program}", xo_program);

    if (xo_style(xop) == XO_STYLE_XML || xo_style(xop) == XO_STYLE_JSON) {
	va_list ap;
	xo_handle_t temp;

	bzero(&temp, sizeof(temp));
	temp.xo_style = XO_STYLE_TEXT;
	xo_buf_init(&temp.xo_data);
	xo_depth_check(&temp, XO_DEPTH);

	va_copy(ap, vap);
	(void) xo_emit_hv(&temp, fmt, ap);
	va_end(ap);

	xo_buffer_t *src = &temp.xo_data;
	xo_format_value(xop, "message", 7, src->xb_bufp,
			src->xb_curp - src->xb_bufp, NULL, 0, NULL, 0, 0);

	xo_free(temp.xo_stack);
	xo_buf_cleanup(src);
    }

    (void) xo_emit_hv(xop, fmt, vap);

    ssize_t len = strlen(fmt);
    if (len > 0 && fmt[len - 1] != '\n') {
	if (code > 0) {
	    const char *msg = strerror(code);
	    if (msg)
		xo_emit_h(xop, ": {G:strerror}{g:error/%s}", msg);
	}
	xo_emit("\n");
    }

    xo_close_marker_h(xop, "xo_emit_warn_hcv");
    xo_flush_h(xop);
}

void
xo_emit_warn_hc (xo_handle_t *xop, int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_warn_hcv(xop, 1, code, fmt, vap);
    va_end(vap);
}

void
xo_emit_warn_c (int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, code, fmt, vap);
    va_end(vap);
}

void
xo_emit_warn (const char *fmt, ...)
{
    int code = errno;
    va_list vap;

    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, code, fmt, vap);
    va_end(vap);
}

void
xo_emit_warnx (const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_warn_hcv(NULL, 1, -1, fmt, vap);
    va_end(vap);
}

void
xo_emit_err_v (int eval, int code, const char *fmt, va_list vap)
{
    xo_emit_warn_hcv(NULL, 0, code, fmt, vap);
    xo_finish();
    exit(eval);
}

void
xo_emit_err (int eval, const char *fmt, ...)
{
    int code = errno;
    va_list vap;
    va_start(vap, fmt);
    xo_emit_err_v(eval, code, fmt, vap);
    /*NOTREACHED*/
}

void
xo_emit_errx (int eval, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_err_v(eval, -1, fmt, vap); /* This will exit */
    /*NOTREACHED*/
}

void
xo_emit_errc (int eval, int code, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);
    xo_emit_err_v(eval, code, fmt, vap); /* This will exit */
    /*NOTREACHED*/
}

/*
 * Get the opaque private pointer for an xo handle
 */
void *
xo_get_private (xo_handle_t *xop)
{
    xop = xo_default(xop);
    return xop->xo_private;
}

/*
 * Set the opaque private pointer for an xo handle.
 */
void
xo_set_private (xo_handle_t *xop, void *opaque)
{
    xop = xo_default(xop);
    xop->xo_private = opaque;
}

/*
 * Get the encoder function
 */
xo_encoder_func_t
xo_get_encoder (xo_handle_t *xop)
{
    xop = xo_default(xop);
    return xop->xo_encoder;
}

/*
 * Get the whiteboard function
 */
xo_whiteboard_func_t
xo_get_wb_marker (xo_handle_t *xop)
{
    xop = xo_default(xop);
    return xop->xo_wb_marker;
}

/*
 * Record an encoder callback function in an xo handle.
 */
void
xo_set_encoder (xo_handle_t *xop, xo_encoder_func_t encoder,
		xo_whiteboard_func_t wb_marker)
{
    xop = xo_default(xop);

    xop->xo_style = XO_STYLE_ENCODER;
    xop->xo_encoder = encoder;
    xop->xo_wb_marker = wb_marker;
}

int
xo_encoder_handle (xo_handle_t *xop, xo_encoder_op_t op, xo_buffer_t *bufp,
		   const char *name, const char *value, xo_xff_flags_t flags)
{
    xo_encoder_func_t func = xo_get_encoder(xop);
    if (func == NULL)
	return -1;

    void *private = xo_get_private(xop);

    if (XOF_ISSET(xop, XOF_FILTER)) {
	xo_filter_status_t fstatus;

	fstatus = xo_filter_passthru(xop, op, bufp, name, value,
				    private, flags, func, xo_filters(xop));

	xo_stack_t *xsp = xo_stack_cur(xop);
	if (fstatus)
	    xsp->xs_fstatus = fstatus;

	return fstatus;
    }

    return func(xop, op, bufp, name, value, private, flags);
}

/*
 * The xo(1) utility needs to be able to open and close lists and
 * instances, but since it's called without "state", we cannot
 * rely on the state transitions (in xo_transition) to DTRT, so
 * we have a mechanism for external parties to "force" transitions
 * that would otherwise be impossible.  This is not a general
 * mechanism, and is really tailored only for xo(1).
 */
void
xo_explicit_transition (xo_handle_t *xop, xo_state_t new_state,
			const char *name, xo_xof_flags_t flags)
{
    xo_xsf_flags_t xsf_flags;

    xop = xo_default(xop);

    switch (new_state) {

    case XSS_OPEN_LIST:
	xo_do_open_list(xop, flags, name);
	break;

    case XSS_OPEN_INSTANCE:
	xo_do_open_instance(xop, flags, name);
	break;

    case XSS_CLOSE_INSTANCE:
	xo_depth_change(xop, name, 1, 1, XSS_OPEN_INSTANCE,
			xo_stack_flags(flags), 0, 0);
	xo_stack_set_flags(xop);
	xo_do_close_instance(xop, name);
	break;

    case XSS_CLOSE_LIST:
	xsf_flags = XOF_ISSET(xop, XOF_NOT_FIRST) ? XSF_NOT_FIRST : 0;

	xo_depth_change(xop, name, 1, 1, XSS_OPEN_LIST,
			XSF_LIST | xsf_flags | xo_stack_flags(flags), 0, 0);
	xo_do_close_list(xop, name);
	break;
    }
}
