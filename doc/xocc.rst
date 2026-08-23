.. index:: xocc

.. _xocc:

The "xocc" compiler wrapper
=============================

:ref:`plugins` describes libxo's two optional LLVM/clang plugins,
`xo_validate` and `xo_precompile`, and how to invoke them by hand with
`-fplugin=` / `-fpass-plugin=`.  Wiring those flags into another
project's build by hand means finding the `.so` paths, the SDK
`-isysroot`, and the `-L/-lxo` link flags, and repeating them on every
compile and link line.

`xocc` is a small shell wrapper, generated at configure time, that
already knows all of that.  It stands in for `$CC` in another
project's build: pass it wherever that project's build system expects
a compiler, and it runs the real compiler with libxo's plugin flags
(and, at link time, `-lxo`) already added.

`xocc` is generated from `llvm-plugins/xocc.sh.in` by `configure`
(via `AC_CONFIG_FILES`) and installed as `${bindir}/xocc` — it only
exists, and only makes sense, in a libxo tree that was configured with
the plugins enabled.

Configuring
------------

`xocc` is only useful if libxo itself was configured with
`--with-llvm-config`, exactly as described in :ref:`plugins`'s
"Building the plugins" section::

    ../configure --with-llvm-config=/opt/local/libexec/llvm-19/bin/llvm-config \
        --prefix ~/work/root

The same `--with-sdk-path=PATH` (or `--with-sdk-path=no`) override
described there applies here too; `xocc` passes the configured SDK
path to the real compiler as `-isysroot` whenever it invokes the LLVM
clang directly.

If `--with-llvm-config` was omitted, the plugins aren't built, `xocc`
still gets generated and installed, but it won't have real plugin
`.so` files to point at.  Build and install libxo normally afterward::

    cd build
    make CFLAGS='-O2'
    make install

This installs `xocc` into `${prefix}/bin` alongside the rest of
libxo.

Invoking "xocc"
-----------------

Unlike a normal `$CC`, `xocc` requires a verb as its first argument,
telling it which plugin flags to add:

- `full` — add both `xo_validate` and `xo_precompile`.
- `validate` — add `xo_validate` only.
- `precompile` — add `xo_precompile` only.

Any other first argument (including a bare compiler flag or a `.c`
file, which means the verb was left off) is an error, and `xocc`
refuses to run rather than silently compiling without libxo's flags.

The verb is consumed before the rest of the command line is handed to
the real compiler, so `xocc` can otherwise be dropped in wherever a
compiler is expected.  The most common use is setting `CC` for a
`make`-based build::

    make CC='xocc full'

The quotes matter — `CC` becomes the two-word command `xocc full`, and
`make` passes the rest of its usual compiler invocation
(`-c foo.c -o foo.o`, or the final link line) after it.  To check
format strings without paying for precompilation, or vice versa, swap
in the narrower verb::

    make CC='xocc validate'
    make CC='xocc precompile'

`xocc` picks which real compiler to invoke, in order:

1. `$XO_REAL_CC`, if set — an explicit override, e.g. when the
   project's build otherwise assumes a specific compiler::

       XO_REAL_CC=/usr/local/bin/clang-19 make CC='xocc full'

2. the LLVM clang that built the plugins (the one found via
   `--with-llvm-config` at libxo's own configure time), since the
   plugins are LLVM-version-specific and only reliably load into a
   matching clang.
3. otherwise, libxo's own build compiler, with the plugin flags added
   anyway if that compiler isn't real gcc (gcc has no equivalent
   plugin support, so if it *is* gcc, the plugin flags are dropped and
   the plain compiler is used unmodified).

`xocc` also inspects the rest of its arguments to tell a compile stage
(`-c`, `-E`, or `-S` present) from a link stage.  At the link stage it
adds `-L${libdir} -lxo` automatically, so linking a program that used
`xocc` for its compiles needs no extra libxo flags::

    xocc full -c myprogram.c -o myprogram.o
    xocc full myprogram.o -o myprogram

Both plugin flags, the SDK `-isysroot`, and the link flags come from
values baked in when libxo itself was configured — `xocc` needs no
`-I`/`-L` flags pointing back at the libxo tree, beyond whatever the
project's own build already passes for libxo's headers.
