# Example: df (excerpts)

From FreeBSD's 2014 df.c; the 2014 conversion is libxo's
patches/df.patch.  The lessons: names must not change with a flag,
humanized and scaled values need an encoded raw value, formats chosen
at run time must still be literals, and a "total" row is not an
instance.

FreeBSD has shipped a libxo df since 2015; converting it again keeps
its shipped names.  This example converts the pre-libxo source.

## Phase 2: schema

    +-- storage-system-information
       +-- filesystem* [name]
       |  +-- name                %s     f_mntfromname
       |  +-- type?               %s     f_fstypename; -T
       |  +-- total-size          %jd    f_blocks * f_bsize, bytes
       |  +-- used-size           %jd    used * f_bsize, bytes
       |  +-- available-size      %jd    f_bavail * f_bsize, bytes
       |  +-- used-percent        %.0f   {U:%}
       |  +-- inodes-used?        %jd    -i
       |  +-- inodes-free?        %jd    -i
       |  +-- inodes-used-percent? %.0f  -i; {U:%}
       |  +-- mounted-on          %s     f_mntonname
       +-- total?                        -c
          +-- total-size          %jd
          +-- ...                        same as filesystem, no name

Notes for the review:

- The text shows sizes as blocks (-k, -m, -g, BLOCKSIZE) or
  humanized (-h, -H).  The encoding always carries bytes, under the
  same names, whatever the flag.  The 2014 patch used "blocks",
  "used", "available" under -h (holding strings like "118M") and
  "total-blocks", "used-blocks", "available-blocks" otherwise.
- "%iused" in the header is a heading, not a name.
- -c adds a row whose "filesystem" is the word "total"; it becomes the
  "total" container, after the list.

## One name, any flag

Before:

    if (hflag) {
        prthuman(sfsp, used);
    } else {
        if (thousands)
            format = " %*j'd %*j'd %*j'd";
        else
            format = " %*jd %*jd %*jd";
        (void)printf(format,
            mwp->total, fsbtoblk(sfsp->f_blocks, sfsp->f_bsize, blocksize),
            mwp->used, fsbtoblk(used, sfsp->f_bsize, blocksize),
            mwp->avail, fsbtoblk(sfsp->f_bavail, sfsp->f_bsize, blocksize));
    }

After:

    if (hflag) {
        prthuman(sfsp, used);
    } else if (thousands) {
        xo_emit(" {d:/%*j'd}{e:total-size/%jd}"
            " {d:/%*j'd}{e:used-size/%jd}"
            " {d:/%*j'd}{e:available-size/%jd}",
            mwp->total, fsbtoblk(sfsp->f_blocks, sfsp->f_bsize, blocksize),
            (intmax_t) sfsp->f_blocks * sfsp->f_bsize,
            mwp->used, fsbtoblk(used, sfsp->f_bsize, blocksize),
            (intmax_t) used * sfsp->f_bsize,
            mwp->avail, fsbtoblk(sfsp->f_bavail, sfsp->f_bsize, blocksize),
            (intmax_t) sfsp->f_bavail * sfsp->f_bsize);
    } else {
        ... the same with "%*jd" ...
    }

The "format" variable becomes two literal calls, so xolint can check
both.  "%'" stays because the original used it.  Since libxo 2.3.0
"%'" is the same as {i:}; before that, {i:} widened a padded field,
so the columns would move (pitfalls.md).  The {e:} fields carry the
plain numbers, so the grouping never reaches the encoding with any
version.

## Humanized values

Before:

    static void
    prthumanval(int64_t bytes)
    {
        char buf[6];
        ...
        humanize_number(buf, sizeof(buf) - (bytes < 0 ? 0 : 1),
            bytes, "", HN_AUTOSCALE, flags);
        (void)printf("  %6s", buf);
    }

2014 patch:

    prthumanval("  {:blocks/%6s}", sfsp->f_blocks * sfsp->f_bsize);
    ...
    xo_attr("value", "%lld", (long long) bytes);
    xo_emit(fmt, buf);

The format is passed in (not a literal at the xo_emit), the encoded
value is the humanized string, and the number is an XML-only
attribute.  After:

    prthumanval(sfsp->f_blocks * sfsp->f_bsize);
    ...
    static void
    prthumanval(int64_t bytes)
    {
        ...
        xo_emit("  {d:/%6s}", buf);
    }

and the caller emits the names:

    xo_emit("{e:total-size/%jd}{e:used-size/%jd}{e:available-size/%jd}",
        (intmax_t) sfsp->f_blocks * sfsp->f_bsize,
        (intmax_t) used * sfsp->f_bsize,
        (intmax_t) sfsp->f_bavail * sfsp->f_bsize);
    prthumanval(sfsp->f_blocks * sfsp->f_bsize);
    prthumanval(used * sfsp->f_bsize);
    prthumanval(sfsp->f_bavail * sfsp->f_bsize);

{h:} cannot replace this before libxo 2.3.0: it ignores the "%6s"
width (pitfalls.md).  Since then it honors the width, but the
"sizeof(buf) - (bytes < 0 ? 0 : 1)" length and the "flags" make the
match uncertain, so keep humanize_number and check with xo-check.

## Percentages and the inode header

Before:

    (void)printf(" %5.0f%%", ...);
    (void)printf(" %*s %*s %%iused", mwp->iused - 2, "iused",
        mwp->ifree, "ifree");

After:

    xo_emit(" {:used-percent/%5.0f}{U:%}", ...);
    xo_emit(" {T:/%*s} {T:/%*s} {T:%iused}", mwp->iused - 2, "iused",
        mwp->ifree, "ifree");

The 2014 patch wrote "{U:%%}" (prints "%%") and "{T:\%iused}" (an
invalid C escape).  Field content is literal: one '%'.

## The total row

Before: with -c, df fills a struct statfs whose f_mntfromname is
"total" and passes it to prtstat(), which prints it like any other
row, skipping the mount point.

After: main calls prtstat() for the total outside the list:

    xo_close_list("filesystem");
    if (cflag) {
        xo_open_container("total");
        prtstat(&totalbuf, &maxwidths);
        xo_close_container("total");
    }

with prtstat() emitting the name as a field only for a real
filesystem:

    if (strncmp(sfsp->f_mntfromname, "total", MNAMELEN) == 0)
        xo_emit("{L:/%-*s}", mwp->mntfrom, sfsp->f_mntfromname);
    else
        xo_emit("{k:name/%-*s/%@*@s}", mwp->mntfrom,
            sfsp->f_mntfromname);

and the instance open and close move from prtstat() to the loop in
main.  The 2014 patch opened the instance inside prtstat(), so the
total became a "filesystem" named "total".
