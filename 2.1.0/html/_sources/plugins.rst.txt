.. index:: plugins

.. _plugins:

LLVM/clang Plugins
===================

libxo ships two optional LLVM/clang plugins that operate on
:func:`xo_emit`-family calls at compile time:

- **xo_validate** — a clang AST plugin that checks `libxo` format
  strings for syntax errors and checks each format string's field
  descriptors against the types and count of the arguments actually
  passed, much like `-Wformat` does for `printf()`.

- **xo_precompile** — an LLVM IR pass that finds `libxo` calls whose
  format string is a compile-time constant, parses that format string
  during the build, and rewrites the call to `xo_emit_cached()`, passing
  a pre-parsed field table as a constant.  This removes the runtime cost
  of parsing the format string on every call.

Both plugins share a single format-string parser (the same one libxo
uses internally, reached through a small C shim so the C++ plugin code
never has to include libxo's non-C++-safe internal headers), so their
understanding of field syntax never drifts from the library's own.

These plugins are entirely optional.  Code built without them runs
exactly as it always has, parsing format strings at run time.  Neither
plugin changes libxo's public behavior or output — `xo_precompile`
specifically only ever changes *when* a format string is parsed, never
*what* it means.

libxo includes `xocc`, a compiler wrapper that already knows
these paths (and the link-time `-lxo` flags); see :ref:`xocc` for
using it instead of setting these flags by hand.

Building the plugins
---------------------

The plugins require LLVM/clang development files (specifically
`llvm-config` and the matching Clang cmake package) and `cmake`.  They
are built as a `cmake` subproject, driven from the normal
autoconf/automake build via `--with-llvm-config`::

    ../configure --with-llvm-config=/opt/local/libexec/llvm-19/bin/llvm-config \
        --prefix ~/work/root

If `--with-llvm-config` is omitted (or `llvm-config` can't be found),
`configure` disables the plugins entirely and the rest of the build is
unaffected.

On macOS, the plugins are built with the same clang that provides
`llvm-config`, which is not Apple's system clang and does not know
where the macOS SDK headers live.  `configure` locates the SDK
automatically via `xcrun`; override it with `--with-sdk-path=PATH`, or
suppress the `-isysroot` flag entirely with `--with-sdk-path=no`::

    ../configure --with-llvm-config=/opt/local/libexec/llvm-19/bin/llvm-config \
        --with-sdk-path=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk

After configuring, build normally; the plugins build alongside the rest
of the tree and install into `${prefix}/lib`::

    cd build
    make CFLAGS='-O2'
    make install

Building just the plugins (skipping the rest of libxo) can be done from
their subdirectory::

    cd build/llvm-plugins
    make

This produces `validate/xo_validate.so` and `precompile/xo_precompile.so`.
Both are `cmake`-driven subprojects; `configure` seeds each one's cmake
cache with the right `LLVM_DIR`, `Clang_DIR`, target architecture, and
install prefix, so plain `make` in that directory is sufficient — there
is no separate `cmake configure` step to run by hand.

xo_validate: format-string checking
-------------------------------------

`xo_validate` is a standard clang `-fplugin` AST plugin.  It runs
alongside the compiler's normal parsing and codegen (it does not
replace or suppress them), inspecting every call to `xo_emit()` and its
variants (`xo_emit_h`, `xo_emit_hf`, `xo_emit_hvf`, the `_p` wrappers,
etc.) whose format string is a compile-time string constant.  For each
one it checks:

1. **Syntax** — malformed field descriptors, such as an unclosed `{`.
2. **Argument count** — too few or too many arguments for the fields
   in the format string.
3. **Argument type** — the type each field's display format expects
   (following the same length-modifier rules as `printf`, e.g. `%ld`
   expects `long`, `%zu` expects `size_t`) is compared against the
   actual argument's type, after the usual varargs promotions.
4. **Style** — a number of `xolint`-style checks on field naming and
   anchor usage.  Leading digits, stray `%` characters, and anchor
   width/format mismatches are always checked; underscores (instead of
   hyphens), upper-case letters, and names shorter than the minimum
   length are checked only when lint mode is enabled (see below).

Diagnostics are reported through clang's normal diagnostic engine, so
they appear exactly like any other compiler diagnostic, with source
location and a caret pointing at the offending call::

    xo_validate_demo.c:22:13: error: libxo: format expects 2 argument(s) but 1 provided
       22 |     xo_emit("{:name/%s} {:age/%d}\n", "bob");
          |             ^

Tuning diagnostics
~~~~~~~~~~~~~~~~~~~~

Two `-mllvm` flags control how `xo_validate` reports problems:

- **`-mllvm -xo-validate-errors-as-warnings`** — by default, syntax,
  argument count, and argument type problems (checks 1-3 above) are
  reported as **errors**, so a build with the plugin loaded fails on
  them.  This is deliberate: these are almost always real bugs, and a
  checker that only ever warns is too easy to ignore.  This flag
  downgrades them to warnings instead, which is useful when first
  introducing the plugin to a codebase with an existing backlog of
  violations, or to keep a build green while an individual false
  positive gets sorted out upstream.  Style diagnostics (check 4) are
  always warnings and are unaffected by this flag.

- **`-mllvm -xo-validate-lint`** — enables the additional cosmetic
  naming checks described under **Style** above (underscores, upper
  case, short names).  These are off by default since they're closer
  to style preference than correctness; the always-on style checks
  (leading digits, anchor mismatches) still run either way.

Both flags can be combined with any of the `-fplugin=` invocations
shown below, e.g.::

    clang -c \
        -fplugin=/path/to/xo_validate.so \
        -mllvm -xo-validate-errors-as-warnings \
        -mllvm -xo-validate-lint \
        -I/path/to/xo/include \
        myprogram.c -o myprogram.o

Invoking `xo_validate`
~~~~~~~~~~~~~~~~~~~~~~~

Invoke the validate plugin using `-fplugin=`, pointing at the built
`.so`:: 

    clang \
        -fplugin=/path/to/xo_validate.so \
        -I/path/to/xo/include \
        -isysroot $(xcrun --show-sdk-path) \
        myprogram.c

It can also be loaded during a normal compile (the diagnostics appear
alongside the compiler's own, and compilation proceeds as usual)::

    clang -c \
        -fplugin=/path/to/xo_validate.so \
        -I/path/to/xo/include \
        myprogram.c -o myprogram.o

To integrate it into a real build, add the two `-fplugin=...` /
`-I...` flags to `CFLAGS` for the translation units you want checked;
there is nothing else to configure or link against.

Since it only inspects the AST and does not need to link, using the
`-fsyntax-only` option is the fastest way to run it standalone, if
only syntax checking is needed.

xo_precompile: build-time format parsing
------------------------------------------

`xo_precompile` is an LLVM **new pass-manager module** plugin, loaded
using `-fpass-plugin=`.  It runs early in the optimization pipeline
(`PipelineStart`, before inlining or constant-folding can obscure a
call or the string it references), and looks for calls to `xo_emit()`
and its variants where the format-string argument resolves to a
constant C string.

For each such call it will:

1. parse the format string with libxo's own parser (via the shared C
   shim), producing the same offset-based field table
   (`xo_field_info_t[]`) and pre-parsed display-format table
   (`xo_fspec_t[]`) that `xo_emit()` would otherwise build at run time,
   on every call.
2. emit those tables as `private constant` LLVM globals in the
   module.
3. rewrite the call to the matching `xo_emit_cached*()` entry point
   (`xo_emit` → `xo_emit_cached`, `xo_emit_hf` → `xo_emit_cached_hf`,
   and so on), passing a pointer to the generated table ahead of the
   format string and value arguments.

At run time, `xo_emit_cached()` skips straight to formatting using the
supplied table instead of first scanning the format string to build
one.  Output is byte-for-byte identical to what `xo_emit()` would have
produced from the same format string and arguments — the pass changes
only when the format string gets parsed, never the result.

If precompiling it isn't possible or safe, calls are left untouched,
falling back to ordinary `xo_emit()` and parsing format strings at run
time.  This is needed when:

- the format string isn't a compile-time constant (e.g. it was built at
  run time, or came from a variable),
- the shim reports a parse error for it (the ordinary runtime path will
  report the same error, so nothing is silently swallowed), or
- the callee isn't one of the known `xo_emit*()` names, or is called
  indirectly through a function pointer.

Because the rewrite happens at the IR level, it works regardless of
what source-level macros or wrappers were used to reach `xo_emit()`, as
long as the call and its format-string argument survive to IR in a form
the pass can see. It does *not* require any source changes — the same
`.c` file compiles correctly whether or not the pass is loaded, just
faster when it is.

Invoking `xo_precompile`
~~~~~~~~~~~~~~~~~~~~~~~~~~

Invoke the precompile module by passing the `-fpass-plugin=` option on
a normal compile; nothing else about the command line changes, and the
resulting object file links against the same `libxo` as an unmodified
build (`xo_emit_cached()` and friends are ordinary exported libxo
symbols, not something the plugin invents)::

    clang -c \
        -fpass-plugin=/path/to/xo_precompile.so \
        -I/path/to/xo/include \
        myprogram.c -o myprogram.o

    clang myprogram.o -L/path/to/xo/lib -lxo -o myprogram

To see the effect directly, dump LLVM IR instead of an object file and
look for the generated `.xo_fields.*` / `.xo_fspecs.*` / `.xo_fcache.*`
globals and the `xo_emit_cached` call::

    clang -S -emit-llvm \
        -fpass-plugin=/path/to/xo_precompile.so \
        -I/path/to/xo/include \
        myprogram.c -o - | grep -A2 xo_fcache

`xo_validate` and `xo_precompile` are independent but can be used
together — one checks correctness at the AST level, the other rewrites
at the IR level — so both flags are normally passed on the same
command line::

    clang -c \
        -fplugin=/path/to/xo_validate.so \
        -fpass-plugin=/path/to/xo_precompile.so \
        -I/path/to/xo/include \
        myprogram.c -o myprogram.o

Wiring into a build
~~~~~~~~~~~~~~~~~~~~

For a project already building against libxo, add the plugin flags to
`CFLAGS`::

    make CFLAGS='-O2 -fplugin=/path/to/xo_validate.so -fpass-plugin=/path/to/xo_precompile.so'

Caveats and known limitations
--------------------------------

- Only calls with a **literal, compile-time-constant** format string
  can be precompiled. A format string built with `sprintf()`,
  concatenation of a runtime value, gettext lookups that select between
  strings at runtime, etc. is left as an ordinary `xo_emit()` call.
- `xo_precompile` currently registers at `PipelineStart`. At `-O0` this
  still fires under the current New Pass Manager, but if a future LLVM
  version changes when `PipelineStart` callbacks run at `-O0`, building
  the precompiled translation units at `-O1` or higher is the fallback.
- Both plugins are LLVM/clang-specific; there is no equivalent for GCC.
  Code is fully portable either way — without the plugins it just
  behaves like ordinary, unmodified libxo.
- The plugins are versioned against `xo_field_info_t`'s in-memory
  layout via `XO_EMIT_CACHE_VERSION` (in `xo.h`) and a set of
  `_Static_assert` layout checks in the shared C shim
  (`llvm-plugins/validate/xo_parse_shim.c`). A libxo built with a
  mismatched header will refuse the precompiled table at run time and
  fall back to normal parsing rather than misinterpreting it.
