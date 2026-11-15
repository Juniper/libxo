.. index:: xohtml

.. _xohtml:

xohtml
======

`xohtml` is a tool for turning the output of libxo-enabled commands into
html files suitable for display in modern HTML web browsers.  It can
be used to test and debug HTML output, as well as to make the user
ache to escape the world of '70s terminal devices.

`xohtml` is given a command, either on the command line or via the "-c"
option.  If not command is given, standard input is used.  The
command's output is wrapped in HTML tags, with references to
supporting CSS and Javascript files, and written to standard output or
the file given in the "-f" option.  The "-b" option can be used to
provide an alternative base path for the support files:

  ============== ===================================================
   Option         Meaning
  ============== ===================================================
   -b <base>      Base path for finding css/javascript files
   -c <command>   Command to execute
   -f <file>      Output file name
  ============== ===================================================

The "-c" option takes a full command with arguments, including
any libxo options needed to generate html (`--libxo=html`).  This
value must be quoted if it consists of multiple tokens.

When the output contains tabular data, `xohtml` adds a "Table view"
button that redraws it as an HTML table, and a "Line view" button
to go back.  Two kinds of output become tables: a title line (all
{T:} fields) followed by data lines, up to a blank line; and two or
more records in a row from the same list with the same fields, whose
column headings are made from the field names ("total-sold" becomes
"Total Sold").

Clicking a column heading sorts the rows by that column, and
clicking it again reverses the order.  Numbers sort as numbers and
other values as text.  A humanized ({h:}) or grouped ({i:} or "%'")
field sorts by the raw value libxo gives in its "data-number"
attribute, so "5K" sorts before "118M" and "1,234,567" sorts as
1234567.
