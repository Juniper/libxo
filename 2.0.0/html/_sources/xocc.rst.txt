.. index:: xocc

.. _xocc:

The "xocc" compiler wrapper
=============================

`xocc` is a small shell wrapper to aid with invoking the C compiler
with using `libxo`'s LLVM plugins.  `xocc` can be used in place of
`cc` in another project's build: pass it wherever that project's build
system expects a compiler, and it will invoke the real C compiler with
libxo's plugin flags as needed.  `libxo` includes LLVM/clang plugins,
described in :ref:`plugins` and `xocc` handles some of the details
needed to invoke them properly.

Invoking "xocc"
-----------------

`xocc` requires a verb as its first argument that describe and adjusts
the actions and behaviors requested.  A verb is one or more tokens
joined with `+` (enabled) or `-` (disabled):

- **validate** - Add the xo_validate plugin to the compiler
  invocation.  (default: disabled)

- **lint** - Add the extra xo_validate style checks
  (--xo-validate-lint); implies `validate`.  (default: disabled)

- **errors** - Report the syntactic and semantic problems found by
  xo_validate as errors instead of warnings.  (default: disabled -
  i.e. warnings by default)

- **precompile** - Add the xo_precompile plugin to the compiler
  invocation.  (default: disabled)

- **sdk** - Add any configured SDK flags to the compiler invocation.
  (default: enabled if the operating system needs it)

- **ldflags** - Add any `ld` flags needed for proper libxo
  compilation.  These are only needed (and passed) at the link stage.
  (default: enabled)

- **echo** - Echo the command line for the underlying compiler
  invocation before execution.  (default: disabled).

The token full is a shorthand for turning on validate, lint, and
precompile together.  It is a provided since this is the typical use
case::

    make CC='xocc full'

Examples
--------

To turn warnings into hard errors, use `errors`:

    make CC='xocc full'

Adding `+errors` will turn warnings into hard errors::n

    make CC='xocc full+errors'

Drop the SDK flag, or drop precompilation, the same way::

    make CC='xocc full-sdk'
    make CC='xocc full-precompile'
    make CC='xocc full-sdk+errors'

Or build a verb up from nothing instead of trimming `full` down — these
two are equivalent::

    make CC='xocc validate+precompile'
    make CC='xocc full-lint'

`lint` on its own (without `validate`, `errors`, or `precompile`) is a
quick check rather than a real build: `xocc` adds `-fsyntax-only`
automatically, since there's nothing to actually compile::

    make CC='xocc lint'

Word and letter forms can be mixed freely, but tokens must be joined
with `+`/`-` — letters can't just be run together.  `v+l+p` is `full`
spelled out; `vlp`, with no separators, is an error.

At least one of `validate`, `lint`, `errors`, or `precompile` has to
end up enabled, or `xocc` refuses to run.  `xocc sdk` on its own, or a
bare compiler flag or `.c` file (meaning the verb was left off
entirely), are both errors too, rather than a silent compile without
libxo's flags.

Add `echo` to see exactly what `xocc` is about to run — often the
fastest way to check a verb combination did what you expected::

    make CC='xocc full+echo'

`xocc` picks which compiler to invoke, but an explicit compiler can be
specified using the `XO_REAL_CC`, environment variable:

       XO_REAL_CC=/usr/local/bin/clang-19 make CC='xocc full'

`xocc` also inspects the rest of its arguments to tell a compile stage
(`-c`, `-E`, or `-S` present) from a link stage.  The `-L${libdir}
-lxo` from the `ldflags` token is only ever added at the link stage —
`xocc` suppresses it automatically for a compile-only invocation even
if `ldflags` is on, since there's nothing to link yet::

    xocc full -c myprogram.c -o myprogram.o
    xocc full myprogram.o -o myprogram

Both plugin flags, the SDK `-isysroot`, and the link flags come from
values baked in when libxo itself was configured — `xocc` needs no
`-I`/`-L` flags pointing back at the libxo tree, beyond whatever the
project's own build already passes for libxo's headers.
