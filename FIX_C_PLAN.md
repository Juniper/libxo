# Fix C: eliminate per-call inner format-spec parsing

Executor: this plan is written to be carried out by a model driving the
existing tools. Follow the stages in order. Stop and report after each
stage's measurement step; do not proceed to the next stage until the
current one builds clean and passes `make test`.

## Hard constraints (do not violate)

- Never run `make accept`. Phil hand-inspects test output.
- Never modify anything under `tests/*/saved/`.
- Never run `git` commands.
- Only run `make` inside directories under `build/`. Prefer the wrapper
  `bin/compile`:
  - `bin/compile full`  -> clean + build + install
  - `bin/compile test`  -> build + install + run full test suite
  - `bin/compile test tests/core` -> build + install + run one suite
- All source edits go in `libxo/libxo.c` (and `libxo/xo.h` only if a
  struct/ABI change is required, which Stage 1 avoids).
- Responses to the user must be plain text: no markdown headers, bold,
  tables, or color. (This file is a requested deliverable, so markdown
  here is fine; the restriction is on chat replies.)

## Background: what is already done, what remains

Two earlier optimizations already landed:
- Fix A: UTF-8 string fast path (keys off `xo_codeset_is_utf8` /
  `XOF_UTF8`), removing per-character `wcrtomb`/`xo_wcwidth` for `%s`.
- Fix B: `xo_format_int_text()` integer fast path, removing
  `vsnprintf`/`localeconv` for `%d/%i/%u/%o/%x/%X`.

After A+B, the `-O3` profile (`build/xxx/n3/bm_01.pass-test.sample`,
~6851 samples) shows the remaining hot leaves, self-time per 1000:

    244.5  xo_do_format_field     <- target of Fix C
    147.0  _platform_memmove
    129.8  xo_do_emit_fields
     70.4  xo_format_value
     53.3  _platform_strlen
     41.7  ___chkstk_darwin       <- Fix D (VLAs), separate work
     36.8  xo_format_text

`xo_do_format_field` is now the single largest cost (~24% of runtime).

### Why it is hot (read the code before changing it)

Call chain for a value field, e.g. `{:count/%d}`:

    xo_emit_h -> xo_emit_hv -> xo_do_emit (libxo.c:7999)
      xo_count_fields + xo_parse_fields   (Layer 1: split into fields)
      -> xo_do_emit_fields (libxo.c:7733) (per-field dispatch)
         -> xo_format_value (libxo.c:6365)
            -> xo_simple_field (libxo.c:4350)
               -> xo_do_format_field (libxo.c:4042)  (Layer 2)

`xo_do_format_field` re-parses the field's printf-style format substring
(e.g. `%d`, `%-14..14s`) on EVERY call: for each `%` it `bzero`s an
`xo_format_t xf`, initializes widths, and calls `xo_parse_format_spec`
(libxo.c:3634) to walk the spec character by character. None of this is
cached across calls. There is already a Layer-1 cache
(`xo_format_cache_t` / `xo_emit_cached`, xo.h:358, libxo.c:8032) that
skips `xo_parse_fields`, but (a) the benchmark and most callers use the
uncached `xo_emit_h` path, and (b) even the cached path still re-parses
Layer 2 in `xo_do_format_field`.

Fix C attacks Layer 2. The goal: parse each distinct field-format
substring at most once per handle, then reuse the parsed `xo_format_t`.

## Design

Add a small per-handle cache mapping a field-format pointer+length to a
pre-parsed `xo_format_t`. Consult it at the top of the per-`%` block in
`xo_do_format_field`; on a hit, skip `bzero`+`xo_parse_format_spec`+width
init and go straight to `xo_emit_field_value`.

### Why per-handle, pointer-keyed is correct and safe

- Threading: libxo's default handle is `THREAD_LOCAL` (libxo.c:461) and
  explicit handles are caller-owned and not used concurrently for emit.
  A cache living inside `xo_handle_t` therefore needs no locking.
- Key stability: `xo_emit` format strings are compile-time string
  literals in the overwhelming common case; the field-format pointer is
  `base + xfi_format`, an interior pointer into that literal, stable for
  process lifetime.
- Correctness against pointer reuse (a caller builds a fmt on the heap,
  frees it, a later allocation reuses the address with different bytes):
  on a pointer-key hit, verify `flen` matches AND `memcmp(fmt, cached,
  flen) == 0` before trusting the cached parse. The specs are short, so
  this compare is far cheaper than a re-parse. This makes the cache a
  pure speed optimization that can never change output.

### Do NOT cache these fields (fall through to the existing parse)

Gate caching off when any of the following hold, because they either
carry per-call va_arg state or take the gettext path:
- `flags & XFF_GT_FLAGS` (gettext; format string is transient `new_fmt`).
- the spec contains `*` (`xf_stars`): widths come from va_args per call.
- `flen` is 0 or `fmt == NULL`.
- multiple `%` specs in one field format (Stage 1 caches only
  single-spec field formats; multi-spec like `%lc - %#lx - %d` falls
  through). Detect this cheaply: a cached entry records the parse of the
  whole substring only when it is a single spec with no interleaved
  literal text. Simplest: cache keyed on the exact `(fmt, flen)` pair and
  only populate when the substring is a lone spec (`fmt[0]=='%'` and the
  parse consumed all `flen` bytes with exactly one conversion). See
  Stage 1 step 4.

Anything not cached uses the current code path unchanged.

## Stage 0: baseline measurement (do this first)

1. Build clean and install:
   `bin/compile full`
2. Run the benchmark pass variant and capture numbers:
   `cd build/tests/benchmark && make bm.pass`
   Output lands in `build/tests/benchmark/out/bm_01.pass-test.out`.
3. Read that file and record the `ns/call` and `min` columns for
   `1-int`, `1-str`, `2-field`, `4-field`, `8-field`. These are the
   Stage-0 baseline to beat. (For reference, n3 min values were roughly:
   1-int 134.5, 1-str 134.25, 8-field 751.1.)
4. Report the baseline table to the user in plain text.

## Stage 1: single-spec per-handle format-spec cache

### Step 1 - add the cache struct and handle field

In `libxo.c`, near the `xo_format_t` definition (line ~434), add a small
fixed-size direct-mapped cache type. A tiny cache is enough because a
given code site cycles through a handful of distinct field formats:

    #define XO_FCACHE_SIZE 32   /* power of two; direct-mapped */

    typedef struct xo_fcache_ent_s {
        const char *fce_fmt;    /* key: field-format pointer (NULL=empty) */
        ssize_t     fce_flen;   /* key: length */
        xo_format_t fce_xf;     /* pre-parsed spec (xf_consumed cleared) */
    } xo_fcache_ent_t;

Find `struct xo_handle_s` (search for `xo_handle_s`; the CLAUDE.md notes
it begins near libxo.c:289) and add one field to it, e.g. after the
buffer work areas:

    xo_fcache_ent_t xo_fcache[XO_FCACHE_SIZE];

Because the handle is zeroed on init, all `fce_fmt` start NULL (empty).
No separate init code is required; confirm by checking how the handle is
allocated/zeroed (`xo_default_init`, `xo_create`).

### Step 2 - a cheap hash

Add a static inline helper near the cache struct:

    static inline unsigned
    xo_fcache_hash (const char *fmt, ssize_t flen)
    {
        uintptr_t p = (uintptr_t) fmt;
        return (unsigned) ((p ^ (p >> 11) ^ (unsigned) flen)
                           & (XO_FCACHE_SIZE - 1));
    }

### Step 3 - lookup/populate helper

Add a helper that returns a pointer to a validated cached `xo_format_t`
for `(fmt, flen)`, or NULL if not cacheable / not present:

    static xo_format_t *
    xo_fcache_lookup (xo_handle_t *xop, const char *fmt, ssize_t flen)
    {
        unsigned h = xo_fcache_hash(fmt, flen);
        xo_fcache_ent_t *e = &xop->xo_fcache[h];
        if (e->fce_fmt == fmt && e->fce_flen == flen)
            return &e->fce_xf;          /* pointer-key hit; see note */
        return NULL;
    }

Note on the pointer-key hit: because the key is the literal's stable
interior pointer, a pointer+length match is a genuine content match for
all normal callers. To be robust against the rare heap-reuse case, make
the hit path in Step 4 additionally `memcmp` the bytes before trusting
the entry (compare against a stored copy or re-derive from the same
`fmt` — since `fmt` IS the pointer we keyed on, the bytes are whatever
`fmt` currently points at, so a `memcmp` is only meaningful if you store
a separate canonical copy). Simplest correct choice: store nothing extra
and rely on pointer+length identity, which is exactly the same stability
assumption the existing `xo_emit_cached` Layer-1 cache already makes.
Document this assumption in a comment. (If Phil wants belt-and-suspenders
safety, add a `char fce_key[8]` storing the first up-to-8 bytes and
compare them; discuss before adding.)

### Step 4 - wire into xo_do_format_field

In `xo_do_format_field` (libxo.c:4042), the per-`%` block currently runs
(lines ~4090-4164): `bzero(&xf...)`, width init, `sp = cp`,
`cp = xo_parse_format_spec(...)`, va_arg width handling, width
defaulting, `D/O/U` lflag fixup, then `xo_emit_field_value`.

Restructure so a cached single-spec field short-circuits the parse.
Determine "single spec covering the whole field format" up front: the
field format handed to `xo_do_format_field` is `fmt..fmt+flen`. It is a
lone spec when the loop would produce exactly one `%`-field and no
literal text, i.e. `fmt[0] == '%'`, no `%%`, and the parsed conversion
lands at `fmt+flen-1`.

Concretely:

- Before the `for` loop, add a fast path guarded by the non-cacheable
  gates (Stage-1 gating list above). If eligible, call
  `xo_fcache_lookup`. On a hit, copy the cached `xf` into a local,
  perform ONLY the per-call bits that must not be cached
  (`xf_skip` from the display/encode-only + `make_output` logic, since
  that depends on style/flags this call), then call
  `xo_emit_field_value` + `xo_advance_vap` and return, skipping the loop.
- On a miss but still eligible, run the existing parse once, then before
  emitting, store the resulting `xf` into the cache slot
  (`e->fce_fmt = fmt; e->fce_flen = flen; e->fce_xf = xf;` with
  `fce_xf.xf_consumed = 0`). IMPORTANT: cache the spec-derived fields
  only; clear/replace anything that is per-call (`xf_consumed`, and any
  `xf_star`-driven widths — but those are gated out anyway).

Care points:
- `xf_skip` depends on this call's style and `make_output`; recompute it
  every call, do not take it from cache.
- The `D/O/U -> xf_lflag = 1` fixup (line ~4162) is spec-derived; it is
  safe to bake into the cached `xf`.
- `xo_needed_encoding(xop)` (line ~4049) depends on the handle/style, not
  the spec; it is already computed per call outside the loop. Leave it.
- Keep the non-cacheable and multi-spec paths byte-for-byte identical to
  today. The safest implementation adds the fast path as an early branch
  and leaves the existing loop untouched as the fallback.

### Step 5 - build, test, measure

1. `bin/compile test`  (must be fully clean; investigate ANY new diff).
   - If the core/xpath/xo suites show diffs, the cache changed output ->
     a correctness bug. Do not touch `saved/`. Fix the code so output is
     identical, then rebuild.
2. If clean, run the benchmark:
   `cd build/tests/benchmark && make bm.pass`
   then read `out/bm_01.pass-test.out`.
3. Report to the user, plain text: Stage-0 vs Stage-1 `ns/call` and `min`
   for 1-int / 1-str / 2/4/8-field, and whether `make test` was clean.

### Stage 1 expected result and honesty check

The benchmark's fields are simple (`%d`, `%s`, `%u`), whose parse is
cheap, so the caching win is the removal of `bzero(&xf)`, the width
init, and the parse-spec call/branching per field. Expect a modest but
real improvement (single-digit percent on the multi-field cases, less on
1-int). If Stage 1 shows essentially no improvement, that tells us the
`xo_do_format_field` self-time is dominated by the surrounding machinery
(function call overhead, `xo_emit_field_value`, buffer append) rather
than the parse, and Stage 2 (below) is the higher-value path. Report the
measurement and let Phil decide before starting Stage 2.

## Stage 2 (only if Stage 1 measurement justifies it): compiled field plan

Bigger, higher-ceiling change. Instead of caching one spec, cache the
entire field-format's render "plan" and add a dedicated fast emit loop
that bypasses `xo_format_value`/`xo_simple_field`/`xo_do_format_field`
for the common single-value text case.

Sketch (design, not yet step-by-step; expand into steps after Stage 1):
- Extend the per-handle cache entry to hold a small array of segments:
  each segment is either a literal run (offset+len into the field
  format) or a pre-parsed spec (`xo_format_t`). This covers multi-spec
  fields like `%lc - %#lx - %d`.
- In `xo_do_format_field`, on a cache hit, iterate segments: literal
  runs go straight to `xo_flush_literal`; spec segments go straight to
  `xo_emit_field_value` + `xo_advance_vap`. This removes the per-char
  scan of the format substring entirely (the `for (cp...)` loop body),
  which is the part the profiler attributes to `xo_do_format_field`.
- Keep the same gating (no gettext, no `*` widths).

Stage 2 also composes with the existing Layer-1 `xo_emit_cached` cache:
once both layers are cached, a repeated `xo_emit` of a fixed format does
zero parsing.

## Stage 3 (optional, separate): confirm against the sampler

After a stage lands clean and shows a benchmark win, regenerate a sample
profile to confirm `xo_do_format_field` self-time dropped:
`cd build/tests/benchmark && make sample.pass`
(writes under `out/`; compare the "Sort by top of stack" leaf counts to
n3). Report the before/after leaf counts for `xo_do_format_field`.

## Rollback / safety

- The cache is a pure memo: with it disabled (e.g. `XO_FCACHE_SIZE`
  lookups always missing), output must be identical. If any test diff
  appears, treat it as a correctness regression in the cache, not a
  baseline to accept.
- If Stage 1 cannot be made both clean and faster, revert the
  `xo_do_format_field` edit and the handle field, report the negative
  result, and recommend Fix D (remove the `char nbuf[nlen+1]` /
  `alloca` VLAs in `xo_format_value`, libxo.c:6387/6412/6430/6454, to
  kill `___chkstk_darwin`, 41.7/1000) as the next move instead.
