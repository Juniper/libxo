#!/usr/bin/env perl
#
# Copyright (c) 2014-2026, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
# Phil Shafer, August 2014
#
#
# xolint -- a lint for inspecting xo_emit format strings
#
# Yes, that's a long way to go for a pun.

%vocabulary = ();
%lexicon = ();
$total_errors = 0;

sub main {
    while ($ARGV[0] =~ /^-/) {
	$_ = shift @ARGV;
	$opt_cpp = 1 if /^-c/;
	$opt_cflags .= shift @ARGV if /^-C/;
	$opt_debug = 1 if /^-d/;
	extract_docs() if /^-D/;
	$opt_info = $opt_vocabulary = 1 if /^-I/;
	$opt_lexicon = $opt_vocabulary = 1 if /^-L/;
	$opt_print = 1 if /^-p/;
	$opt_vocabulary = 1 if /^-V/;
	extract_samples() if /^-X/;
    }

    if ($#ARGV < 0) {
	print STDERR "xolint [options] files ...\n";
	print STDERR "    -c    invoke 'cpp' on input\n";
	print STDERR "    -C flags   Pass flags to cpp\n";
	print STDERR "    -d         Show debug output\n";
	print STDERR "    -D         Extract xolint documentation\n";
	print STDERR "    -I         Print xo_info_t data\n";
	print STDERR "    -L         Print lexicon (tags and use counts)\n";
	print STDERR "    -p         Print input data on errors\n";
	print STDERR "    -V         Print vocabulary (list of tags)\n";
	print STDERR "    -X         Print examples of invalid use\n";
	exit(1);
    }

    for $file (@ARGV) {
	parse_file($file);
    }

    if ($opt_lexicon) {
	for $name (sort(keys(%lexicon))) {
	    print $name, " ", $lexicon{$name}, "\n";
	}
    } elsif ($opt_info) {
	print "static xo_info_t xo_info_table[] = {\n";
	for $name (sort(keys(%vocabulary))) {
	    print "    { \"", $name, "\", \"type\", \"desc\" },\n";
	}
	print "};\n";
	print "static int xo_info_count = "
	    . "(sizeof(xo_info_table) / sizeof(xo_info_table[0]));\n\n";
	print "#define XO_SET_INFO() \\\n";
	print "    xo_set_info(NULL, xo_info_table, xo_info_count)\n";
    } elsif ($opt_vocabulary) {
	for $name (sort(keys(%vocabulary))) {
	    print $name, "\n";
	}
    }

    # Let a makefile know that we found something
    exit(1) if $total_errors;
}

sub extract_samples {
    my $x = "\#" . "\@";
    my $cmd = "grep -B1 -i '$x Should be' $0 | grep xo_emit | sed 's/.*\#*\@//'";
    system($cmd);
    exit(0);
}

sub extract_docs {
    my $x = "\#" . "\@";
    my $cmd = "grep -B1 '$x' $0";
    open INPUT, "$cmd |";
    local @input = <INPUT>;
    close INPUT;
    my $ln, $new = 0, $first = 1, $in_code = 0, $code_indent;
    my @sections, $title, $text;

    for ($ln = 0; $ln <= $#input; $ln++) {
	chomp($_ = $input[$ln]);
	if (/^--/) {
	    $ln += 1;
	    $new = 1;
	    next;
	}
	if ($first) {
	    $new = 1;
	    $first = 0;
	    next;
	}

	# Indented lines are code; keep indentation past the first line
	my $code = 0;
	if (/^\s*\#\@( {2,})/ || /xo_emit\s*\(/) {
	    my $len = length($1);
	    $code_indent = $len unless $in_code;
	    $len = $code_indent if $len > $code_indent;
	    s/^\s*\#\@ {$len}//;
	    s/^\s+// if $len == 0;
	    $code = 1;
	} else {
	    s/^\s*\#\@\s*//;
	}

	if ($in_code && !$code) {
	    $text .= "\n";
	    $in_code = 0;
	}

	if ($new) {
	    push(@sections, [ $title, $text ]) if defined $title;
	    $title = $_;
	    $under = "+" x (length($_) + 2);

	    $text = "'$_'\n$under\n\n";
	    $text .= "The message \"$_\" can be caused by code like:\n";
	    $new = 0;

	} elsif ($code) {
	    # "like:" before a code block becomes "like::"
	    if (!$in_code) {
		$text .= "\n" unless $text =~ s/:\n$/::\n\n/;
		$text .= "::\n\n" unless $text =~ /::\n\n$/;
	    }
	    $text .= "    $_\n";
	    $in_code = 1;

	} elsif (/^Should be/i) {
	    $text .= "This code should be replaced with code like:\n";

	} else {
	    $text .= "$_\n";
	}
    }
    push(@sections, [ $title, $text ]) if defined $title;

    # Sort the sections by title, two blank lines between them
    my @sorted = sort { lc($a->[0]) cmp lc($b->[0]) || $a->[0] cmp $b->[0] }
	@sections;
    for my $sec (@sorted) {
	$sec->[1] =~ s/\n+$/\n/;
    }
    print join("\n\n", map { $_->[1] } @sorted);

    exit(0);
}

sub parse_file {
    local($file) = @_;
    local($errors, $warnings, $info) = (0, 0, 0);
    local $curfile = $file;
    local $curln = 0;

    if ($opt_cpp) {
	die "no such file" unless -f $file;
	open INPUT, "cpp $opt_cflags $file |";
    } else {
	open(INPUT, "<", $file)
	    || die "xolint: cannot open input file '$file': $!\n";
    }
    local @input = <INPUT>;
    close INPUT;

    local $ln, $rln, $line, $replay;

    for ($ln = 0; $ln <= $#input; $ln++) {
	$line = $input[$ln];
	$curln += 1;

	if ($line =~ /^\#/) {
	    my($num, $fn) = ($line =~ /\#\s*(\d+)\s+"(.+)"/);
	    ($curfile, $curln) = ($fn, $num) if $num;
	    next;
	}

	# For the lexicon, the names given to containers, lists, and
	# attributes are tags too.  Instances are skipped, since an
	# instance's name is its list's name.
	if ($opt_lexicon
	        && $line =~ /\bxo_(?:open_(?:container|list)|attr)(?:_\w+)?\s*\(/) {
	    lexicon_call();
	    next;
	}

	check_parse_args() if !$opt_vocabulary
	    && $line =~ /\bxo_parse_args\s*\(/;

	# All the xo_emit variants, like xo_emit_h and xo_emit_warn, take
	# a format string.  xo_emit_field does not; its strings are the
	# pieces of a single field.
	next unless $line =~ /xo_emit(?:r|_(?!field)\w+)?\(/;

	@tokens = lexicon_parse_tokens();
	next unless @tokens;
	print "token:\n    '" . join("'\n    '", @tokens) . "'\n"
	    if $opt_debug;

	# The format follows any handle, flags or exit code arguments,
	# so it is the first argument that is a string
	my($format) = grep { /^".*"$/ } @tokens;
	if ($opt_lexicon) {
	    if (!defined($format)) {
		lexicon_note("format is not a string literal");
		next;
	    }

	    # A macro argument pasted between literals ("{:" f "/%u}")
	    # leaves a bare quote inside; the tag isn't knowable here
	    if (substr($format, 1, -1) =~ /(?<!\\)"/) {
		lexicon_note("format pastes in a macro argument");
		next;
	    }
	}

	#@ Format is not a literal; it cannot be checked
	#@     xo_emit(fmt, value);
	#@ Should be:
	#@     xo_emit("{a:/%s}", name, value);
	#@ A format built at run time (with snprintf, say) can't be
	#@ checked by xolint or the compiler, its field names can't be
	#@ put in a lexicon, and a "{" in the data is a format-string
	#@ injection.  libxo has features (the "a" modifier, "*" widths,
	#@ "%@*@") so formats never need to be built; choose between
	#@ literal formats instead.
	if (!defined($format)) {
	    warning("format is not a literal; it cannot be checked");
	    next;
	}

	check_format($format);
    }

    print $file . ": $errors errors, $warnings warnings, $info info\n"
	unless $opt_vocabulary;

    $total_errors += $errors;
}

sub parse_tokens {
    my $full = "$'";
    my @tokens = ();
    my %pairs = ( "{" => "}", "[" => "]", "(" => ")" );
    my %quotes = ( "\"" => "\"", "'" => "'" );
    local @data = split(//, $full);
    local @open = ();
    local $current = "";
    my $quote = "";
    local $off = 0;
    my $ch;

    $replay = $curln . "     " . $line;
    $rln = $ln + 1;

    for (;;) {
	get_tokens() if $off > $#data;
	die "out of data" if $off > $#data;
	$ch = $data[$off++];

	print "'$ch' ($quote) ($#open) [" . join("", @open) . "]\n"
	    if $opt_debug;

	last if $ch eq ";" && $#open < 0;

	if ($ch eq "," && $quote eq "" && $#open < 0) {
	    print "[$current]\n" if $opt_debug;
	    push @tokens, $current;
	    $current = "";
	    next;
	}

	next if $ch =~ /[ \t\n\r]/ && $quote eq "" && $#open < 0;

	$current .= $ch;

	if ($quote) {
	    if ($ch eq $quote) {
		$quote = "";
	    }
	    next;
	}
	if ($quotes{$ch}) {
	    $quote = $quotes{$ch};
	    $current = substr($current, 0, -2) if $current =~ /""$/;
	    next;
	}

	if ($pairs{$ch}) {
	    push @open, $pairs{$ch};
	    next;
	}

	if ($#open >= 0 && $ch eq $open[$#open]) {
	    pop @open;
	    next;
	}
    }

    push @tokens, substr($current, 0, -1);
    return @tokens;
}

sub get_tokens {
    if ($ln + 1 <= $#input) {
	$line = $input[++$ln];
	$curln += 1;
	$replay .= $curln . "     " . $line;
	@data = split(//, $line);
	$off = 0;
    }
}

sub check_format {
    my($format) = @_;

    return unless $format =~ /^".*"$/;

    my @data = split(//, $format);
    my $ch;
    my $braces = 0;
    local $count = 0;
    my $content = "";
    my $off;
    my $phase = 0;
    my @build = ();
    local $last, $prev = "";

    # Nukes quotes
    pop @data;
    shift @data;

    for (;;) {
	last if $off > $#data;
	$ch = $data[$off++];

	if ($ch eq "\\") {
	    $ch = $data[$off++];
	    if ($ch eq "\\") {
		$off += 1;	# double backslash: "\\/"

		# Keep a placeholder for an escaped character in a
		# format, so a boolean pair like "\\++off" keeps its word
		$build[$phase] .= "x" if $braces && $phase >= 2;
	    }
	    next;
	}

	if ($braces) {
	    if ($ch eq "}") {
		check_field(@build);
		$braces = 0;
		@build = ();
		$phase = 0;
		next;
	    } elsif ($phase == 0 && $ch eq ":") {
		$phase += 1;
		next;
	    } elsif ($ch eq "/") {
		$phase += 1;
		next;
	    }

	} else {
	    if ($ch eq "{" && $data[$off] eq "{") {
		# "{{" is an escaped brace, not the start of a field
		$off += 1;
		$build[0] .= $ch;
		next;
	    }

	    if ($ch eq "{") {
		check_text($build[0]) if length($build[0]);
		$braces = 1;
		@build = ();
		$last = $prev;
		next;
	    }
	    $prev = $ch;
	}

	$build[$phase] .= $ch;
    }

    if ($braces) {
	error("missing closing brace");
	check_field(@build);
    } else {
	check_text($build[0]) if length($build[0]);
    }
}

sub check_text {
    my($text) = @_;

    print "checking text: [$text]\n" if $opt_debug;

    #@ A percent sign appearing in text is a literal
    #@     xo_emit("cost: %d", cost);
    #@ Should be:
    #@     xo_emit("{L:cost}: {:cost/%d}", cost);
    #@ This can be a bit surprising and could be a field that was not
    #@ properly converted to a libxo-style format string.
    if ($text =~ /%/ && $text !~ /%%/) {
	info("a percent sign appearing in text is a literal");
	return;
    }

    #@ A percent sign appearing in text is a literal, so %% prints two
    #@     xo_emit("{:used/%d}%%\n", used);
    #@ Should be:
    #@     xo_emit("{:used/%d}%\n", used);
    #@ Outside a field, text is copied as is; "%" needs no escaping,
    #@ and a "%%" left over from a printf format prints two percent
    #@ signs.
    info("a percent sign appearing in text is a literal, so %% prints two")
	if $text =~ /%%/;
}

sub check_parse_args {
    my $cur = $line;
    my $i;

    #@ The result of xo_parse_args is ignored
    #@     xo_parse_args(argc, argv);
    #@ Should be:
    #@     argc = xo_parse_args(argc, argv);
    #@     if (argc < 0)
    #@         exit(EXIT_FAILURE);
    #@ xo_parse_args returns the new argc, with the libxo options
    #@ removed, or -1 for an invalid option.  Ignoring it leaves the
    #@ "--libxo" options in argv, and carries on after an error.
    my($var) = ($cur =~ /([\w.>\[\]-]+)\s*=\s*xo_parse_args\s*\(/);
    if (!defined($var)) {
	return if $cur =~ /\(\s*xo_parse_args\s*\(/;	# Tested in place
	return if $cur =~ /(?:return|[<>=!]=?)\s*xo_parse_args\s*\(/;
	warning("the result of xo_parse_args is ignored");
	return;
    }

    #@ The result of xo_parse_args is not checked for failure
    #@     argc = xo_parse_args(argc, argv);
    #@     xo_set_version("1.0");
    #@ Should be:
    #@     argc = xo_parse_args(argc, argv);
    #@     if (argc < 0)
    #@         exit(EXIT_FAILURE);
    #@ xo_parse_args returns -1 for an invalid "--libxo" option; the
    #@ program must stop rather than run with a negative argc.
    my $qvar = quotemeta($var);
    my $rest = $cur;
    $rest =~ s/.*?xo_parse_args\s*\(//;
    for ($i = 0; $i <= 8; $i++) {
	return if $rest =~ /$qvar\s*(?:<\s*0|<=\s*-1|==\s*-1|<\s*1\b|<=\s*0)/;
	return if $rest =~ /(?:0\s*>|-1\s*==)\s*$qvar\b/;
	last if $ln + $i + 1 > $#input;
	$rest = $input[$ln + $i + 1];
	last if $rest =~ /(?<![\w.>])$qvar\s*=[^=]/;	# Reassigned
    }

    warning("the result of xo_parse_args is not checked for failure");
}

%short = (
    # Roles
    "boolean" => "B",
    "color" => "C",
    "decoration" => "D",
    "error" => "E",
    "format" => "F",
    "label" => "L",
    "note" => "N",
    "padding" => "P",
    "title" => "T",
    "units" => "U",
    "value" => "V",
    "warning" => "W",
    "start-anchor" => "[",
    "stop-anchor" => "]",
    # Modifiers; "@" marks one that has no short version
    "argument" => "a",
    "colon" => "c",
    "comma" => "@",
    "dense" => "@",
    "display" => "d",
    "drop-plus" => "@",
    "encoding" => "e",
    "escape-private" => "@",
    "escape-slash" => "@",
    "escape-square" => "@",
    "first-cap" => "f",
    "full-boolean" => "@",
    "gettext" => "g",
    "hn" => "h",
    "hn-decimal" => "@",
    "hn-space" => "@",
    "hn-1000" => "@",
    "humanize" => "h",
    "int-group" => "i",
    "key" => "k",
    "leaf-list" => "l",
    "list" => "l",
    "no-quote" => "n",
    "no-quotes" => "n",
    "plural" => "p",
    "quote" => "q",
    "quotes" => "q",
    "trim" => "t",
    "units-attr" => "@",
    "white" => "w",
 );

sub check_field {
    my(@field) = @_;
    print "checking field: [" . join("][", @field) . "]\n" if $opt_debug;

    if ($field[0] =~ /,/) {
	# We have long names; deal with it by turning them into short names
	my @parts = split(/,/, $field[0]);
	my $new = "";
	for (my $i = 1; $i <= $#parts; $i++) {
	    my $v = $parts[$i];
	    $v =~ s/^\s+//;
	    $v =~ s/\s+$//;
	    if ($short{$v} eq "@") {
		# ignore; has no short version
	    } elsif ($short{$v}) {
		$new .= $short{$v};
	    } else {
		#@ Unknown long name for role/modifier
		#@   xo_emit("{,humanization:value}", value);
		#@ Should be:
		#@   xo_emit("{,humanize:value}", value);
		#@ The hn-* modifiers (hn-decimal, hn-space, hn-1000)
		#@ are only valid for fields with the {h:} modifier.
		error("Unknown long name for role/modifier ($v)");
	    }
	}

	$field[4] = substr($field[0], index($field[0], ","));
	$field[0] = $parts[0] . $new;
    }

    if ($opt_lexicon) {
	lexicon_field(@field);
	return;
    }

    if ($opt_vocabulary) {
	$vocabulary{$field[1]} = 1
	    if $field[1] && $field[0] !~ /[DELNPTUW\[\]]/;
	return;
    }

    #@ Last character before field definition is a field type
    #@ A common typo:
    #@     xo_emit("{T:Min} T{:Max}");
    #@ Should be:
    #@     xo_emit("{T:Min} {T:Max}");
    #@ Twiddling the "{" and the field role is a common typo.
    info("last character before field definition is a field type ($last)")
	if $last =~ /[BDELNPTUVW\[\]]/ && $field[0] !~ /[BDELNPTUVW\[\]]/;

    #@ Encoding format uses different number of arguments
    #@     xo_emit("{:name/%6.6s %04d/%s}", name, number);
    #@ Should be:
    #@     xo_emit("{:name/%6.6s %04d/%s-%d}", name, number);
    #@ Both format should consume the same number of arguments off the stack
    my $cf = count_args($field[2]);
    my $ce = count_args($field[3]);
    warning("encoding format uses different number of arguments ($cf/$ce)")
	if $ce >= 0 && $cf >= 0 && $ce != $cf;

    #@ Only one field role can be used
    #@     xo_emit("{LT:Max}");
    #@ Should be:
    #@     xo_emit("{T:Max}");
    my %seen;
    my(@roles) = grep { !$seen{$_}++ } ($field[0] =~ /[BCDEFGLNPTUVW\[\]]/g);
    error("only one field role can be used (" . join(", ", @roles) . ")")
	if $#roles > 0;

    #@ A percent sign in literal content is a literal, so %% prints two
    #@     xo_emit("{:used/%d}{U:%%}\n", used);
    #@ Should be:
    #@     xo_emit("{:used/%d}{U:%}\n", used);
    #@ The content of a decoration, label, note, title or units field
    #@ is copied as is, so "{U:%%}" prints "%%" (and sets units="%%").
    info("a percent sign in literal content is a literal, so %% prints two")
	if $field[0] =~ /[DLNTU]/ && $field[1] =~ /%%/;

    # Field is a color, note, label, or title
    if ($field[0] =~ /[CDLNT]/) {

	#@ Potential missing slash after C, D, N, L, or T with format
	#@     xo_emit("{T:%6.6s}\n", "Max");
	#@ should be:
	#@     xo_emit("{T:/%6.6s}\n", "Max");
	#@ The "%6.6s" will be a literal, not a field format.  While
	#@ it's possibly valid, it's likely a missing "/".  A percent
	#@ sign that does not start a conversion ("{D:%}", "{L:100%}")
	#@ is not reported.
	info("potential missing slash after C, D, N, L, or T with format")
	    if $field[1] =~ /%[-+ #0-9.*'\@]*[A-Za-z]/;

	#@ An encoding format cannot be given (roles: DNLT)
	#@    xo_emit("{T:Max//%s}", "Max");
	#@ Fields with the C, D, N, L, and T roles are not emitted in
	#@ the 'encoding' style (JSON, XML), so an encoding format
	#@ would make no sense.
	error("encoding format cannot be given when content is present")
	    if $field[3];
    }

    # Field is a color, decoration, label, or title
    if ($field[0] =~ /[CDLN]/) {
	#@ Format cannot be given when content is present (roles: CDLN)
	#@    xo_emit("{N:Max/%6.6s}", "Max");
	#@ Fields with the C, D, L, or N roles can't have both
	#@ static literal content ("{L:Label}") and a
	#@ format ("{L:/%s}").
	#@ This error will also occur when the content has a backslash
	#@ in it, like "{N:Type of I/O}"; backslashes should be escaped,
	#@ like "{N:Type of I\\/O}".  Note the double backslash, one for
	#@ handling 'C' strings, and one for libxo.
	error("format cannot be given when content is present")
	    if $field[1] && $field[2];
    }

    # Field is a color/effect
    if ($field[0] =~ /C/) {
	if ($field[1]) {
	    my $val;
	    my @sub = split(/,/, $field[1]);
	    grep { s/^\s*//; s/\s*$//; } @sub;

	    for $val (@sub) {
		if ($val =~ /^(default|black|red|green|yellow|blue|magenta|cyan|white)$/) {

		    #@ Field has color without fg- or bg- (role: C)
		    #@   xo_emit("{C:green}{:foo}{C:}", x);
		    #@ Should be:
		    #@   xo_emit("{C:fg-green}{:foo}{C:}", x);
		    #@ Colors must be prefixed by either "fg-" or "bg-".
		    error("Field has color without fg- or bg- (role: C)");

		} elsif ($val =~ /^(fg|bg)-(default|black|red|green|yellow|blue|magenta|cyan|white)$/) {
		    # color
		} elsif ($val =~ /^(bold|underline)$/) {
		} elsif ($val =~ /^(no-)?(bold|underline|inverse)$/) {
		    # effect

		} elsif ($val =~ /^(reset|normal)$/) {
		    # effect also
		} else {
		    #@ Field has invalid color or effect (role: C)
		    #@   xo_emit("{C:fg-purple,bold}{:foo}{C:gween}", x);
		    #@ Should be:
		    #@   xo_emit("{C:fg-red,bold}{:foo}{C:fg-green}", x);
		    #@ The list of colors and effects are limited.  The
		    #@ set of colors includes default, black, red, green,
		    #@ yellow, blue, magenta, cyan, and white, which must
		    #@ be prefixed by either "fg-" or "bg-".  Effects are
		    #@ limited to bold, no-bold, underline, no-underline,
		    #@ inverse, no-inverse, normal, and reset.  Values must
		    #@ be separated by commas.
		    error("Field has invalid color or effect (role: C) ($val)");
		}
	    }
	}
    }

    # Humanized field
    if ($field[0] =~ /h/) {
	if (length($field[2]) == 0) {
	    #@ Field has humanize modifier but no format string
	    #@   xo_emit("{h:value}", value);
	    #@ Should be:
	    #@   xo_emit("{h:value/%d}", value);
	    #@ Humanization is only value for numbers, which are not
	    #@ likely to use the default format ("%s").
	    error("Field has humanize modifier but no format string");
	}
    }

    # hn-* on non-humanize field
    if ($field[0] !~ /h/) {
	if ($field[4] =~ /,hn-/) {
	    #@ Field has hn-* modifier but not 'h' modifier
	    #@   xo_emit("{,hn-1000:value}", value);
	    #@ Should be:
	    #@   xo_emit("{h,hn-1000:value}", value);
	    #@ The hn-* modifiers (hn-decimal, hn-space, hn-1000)
	    #@ are only valid for fields with the {h:} modifier.
	    error("Field has hn-* modifier but not 'h' modifier");
	}
    }

    my $full_boolean = ($field[4] =~ /,\s*full-boolean\s*(,|$)/);

    if ($field[0] =~ /B/) {
	check_boolean(\@field, $full_boolean);

    } elsif ($full_boolean) {
	#@ The 'full-boolean' modifier is only valid for 'B' fields
	#@     xo_emit("{V,full-boolean:enabled}", "yes");
	#@ Should be:
	#@     xo_emit("{B,full-boolean:enabled}", "yes");
	#@ The 'full-boolean' modifier controls how a boolean field
	#@ is encoded, so it makes no sense for other roles.
	error("'full-boolean' is only valid for 'B' fields");
    }

    # A value field, which is what a field with no other role is.  With
    # the argument modifier the name is not in the format string, so
    # there is nothing for us to check.
    if ($field[0] !~ /[CDEFGLNPTUW\[\]]/ && $field[0] !~ /a/) {

	#@ Value field must have a name (as content)")
	#@     xo_emit("{:/%s}", "value");
	#@ Should be:
	#@     xo_emit("{:tag-name/%s}", "value");
	#@ The field name is used for XML and JSON encodings.  These
	#@ tags names are static and must appear directly in the
	#@ field descriptor.
	# A display-only field is never encoded, so it needs no name
	error("value field must have a name (as content)")
	    unless $field[1] || $field[0] =~ /d/;

	#@ Use hyphens, not underscores, for value field name
	#@     xo_emit("{:no_under_scores}", "bad");
	#@ Should be:
	#@     xo_emit("{:no-under-scores}", "bad");
	#@ Use of hyphens is traditional in XML, and the XOF_UNDERSCORES
	#@ flag can be used to generate underscores in JSON, if desired.
	#@ But the raw field name should use hyphens.
	error("use hyphens, not underscores, for value field name")
	    if $field[1] =~ /_/;

	#@ Value field name cannot start with digit
	#@     xo_emit("{:10-gig/}");
	#@ Should be:
	#@     xo_emit("{:ten-gig/}");
	#@ XML element names cannot start with a digit.
	error("value field name cannot start with digit")
	    if $field[1] =~ /^[0-9]/;

	#@ Value field name should be lower case
	#@     xo_emit("{:WHY-ARE-YOU-SHOUTING}", "NO REASON");
	#@ Should be:
	#@     xo_emit("{:why-are-you-shouting}", "no reason");
	#@ Lower case is more civilized.  Even TLAs should be lower case
	#@ to avoid scenarios where the differences between "XPath" and
	#@ "Xpath" drive your users crazy.  Lower case rules the seas.
	error("value field name should be lower case")
	    if $field[1] =~ /[A-Z]/;

	#@ Value field name should be longer than two characters
	#@     xo_emit("{:x}", "mumble");
	#@ Should be:
	#@     xo_emit("{:something-meaningful}", "mumble");
	#@ Field names should be descriptive, and it's hard to
	#@ be descriptive in less than two characters.  Consider
	#@ your users and try to make something more useful.
	#@ Note that this error often occurs when the field type
	#@ is placed after the colon ("{:T/%20s}"), instead of before
	#@ it ("{T:/20s}").
	error("value field name should be longer than two characters")
	    if $field[1] && length($field[1]) <= 2;

	#@ Value field name contains invalid character
	#@     xo_emit("{:cost-in-$$/%u}", 15);
	#@ Should be:
	#@     xo_emit("{:cost-in-dollars/%u}", 15);
	#@ An invalid character is often a sign of a typo, like "{:]}"
	#@ instead of "{]:}".  Field names are restricted to lower-case
	#@ characters, digits, and hyphens.
	error("value field name contains invalid character (" . $field[1] . ")")
	    unless $field[1] =~ /^[0-9a-z-]*$/;
    }

    # A decoration field
    if ($field[0] =~ /D/) {

	#@decoration field contains invalid character
	#@     xo_emit("{D:not good}");
	#@ Should be:
	#@     xo_emit("{D:((}{:good}{D:))}", "yes");
	#@ This is minor, but fields should use proper roles.  Decoration
	#@ fields are meant to hold punctuation and other characters used
	#@ to decorate the content, typically to make it more readable
	#@ to human readers, and to let CSS style them apart from the
	#@ text.  Any punctuation is fine ("{D:,}", "{D:\\/}", "{D:->}"),
	#@ as is a format ("{D:/%c}"); letters and digits belong in
	#@ another role.
	warning("decoration field contains invalid character")
	    if $field[1] =~ /[A-Za-z0-9]/;
    }

    if ($field[0] =~ /[\[\]]/) {
	#@ Anchor content should be decimal width
	#@     xo_emit("{[:mumble}");
	#@ Should be:
	#@     xo_emit("{[:32}");
	#@ Anchors need an integer value to specify the width of
	#@ the set of anchored fields.  The value can be positive
	#@ (for left padding/right justification) or negative (for
	#@ right padding/left justification) and can appear in
	#@ either the start or stop anchor field descriptor.
	error("anchor content should be decimal width")
	    if $field[1] && $field[1] !~ /^-?\d+$/ ;

	#@ Anchor format should be "%d"
	#@     xo_emit("{[:/%s}");
	#@ Should be:
	#@     xo_emit("{[:/%d}");
	#@ Anchors only grok integer values, and if the value is not static,
	#@ if must be in an 'int' argument, represented by the "%d" format.
	#@ Anything else is an error.
	error("anchor format should be \"%d\"")
	    if $field[2] && $field[2] ne "%d";

	#@ Anchor cannot have both format and encoding format")
	#@     xo_emit("{[:32/%d}");
	#@ Should be:
	#@     xo_emit("{[:32}");
	#@ Anchors can have a static value or argument for the width,
	#@ but cannot have both.
	error("anchor cannot have both format and encoding format")
	    if $field[1] && $field[2];
    }
}

#
# Check a boolean ('B') field.  The display format is an optional
# boolean pair ("true+false" words, or "*" for a pair passed as an
# argument) followed by an optional conversion spec.  An encoding
# format has the same shape, but needs both parts; it gives the words
# for the encoding styles.
#
sub check_boolean {
    my($fieldp, $full_boolean) = @_;
    my @field = @$fieldp;

    my($dpair, $dstar, $dspec) = check_boolean_format($field[2], 0);
    return unless defined $dpair;

    #@ Humanize is ignored for 'B' fields
    #@     xo_emit("{Bh:enabled/%d}", enabled);
    #@ Should be:
    #@     xo_emit("{B:enabled/%d}", enabled);
    #@ A boolean field displays one of two words, so there is no
    #@ number to humanize.
    warning("'humanize' is ignored for 'B' fields")
	if $field[0] =~ /h/;

    #@ Int-group is ignored for 'B' fields
    #@     xo_emit("{Bi:enabled/%d}", enabled);
    #@ Should be:
    #@     xo_emit("{B:enabled/%d}", enabled);
    #@ A boolean field displays one of two words, so there is no
    #@ number to group.
    warning("'int-group' is ignored for 'B' fields")
	if $field[0] =~ /i/;

    if (length($field[3])) {
	my($epair, $estar, $espec) = check_boolean_format($field[3], 1);
	return unless defined $epair;

	#@ Boolean encoding format must take the same arguments
	#@     xo_emit("{B,full-boolean:enabled/yes+no%d/1+0%p}", enabled);
	#@ Should be:
	#@     xo_emit("{B,full-boolean:enabled/yes+no%d/1+0%d}", enabled);
	#@ The encoding format is applied to the same arguments as
	#@ the display format, so both must have the same shape: the
	#@ same "*" pair (or none), the same stars, and the same
	#@ argument type.
	error("boolean encoding format must take the same arguments "
	      . "as the display format")
	    if $dstar != $estar || boolean_spec_type($dspec)
		ne boolean_spec_type($espec);

    } elsif ($full_boolean && !$dpair) {
	#@ The 'full-boolean' modifier without a boolean pair gives quoted true/false
	#@     xo_emit("{B,full-boolean:enabled/%d}", enabled);
	#@ Should be:
	#@     xo_emit("{B,full-boolean:enabled/yes+no%d}", enabled);
	#@ The 'full-boolean' modifier encodes the boolean pair's words
	#@ instead of a native true/false value.  With no pair, the
	#@ words are "true" and "false", encoded as strings.
	warning("'full-boolean' without a boolean pair gives quoted "
		. "true/false");
    }
}

#
# Check one boolean format, returning (has-pair, star-pair, spec), or
# an empty list on error.  The spec is "" when there is none.
#
sub check_boolean_format {
    my($format, $complete) = @_;

    # The pair is the literal run before the spec; "%%" is a literal
    my($pair, $rest) = ($format =~ /^((?:%%|[^%])*)(.*)$/s);
    my $star = ($pair eq "*");
    my $spec = "";

    if (length($pair) && !$star) {
	my $pluses = ($pair =~ tr/+//);

	#@ Boolean pair must have exactly one '+'
	#@     xo_emit("{B:enabled/yes%d}", enabled);
	#@ Should be:
	#@     xo_emit("{B:enabled/yes+no%d}", enabled);
	#@ A boolean pair gives the "true" word, a plus sign, and the
	#@ "false" word.  Use a backslash to put a literal "+" in a
	#@ word ("{B:mode/a\\+b+c%d}").  At run time, libxo reports
	#@ a broken pair and displays "true" or "false" instead.
	if ($pluses != 1) {
	    error("boolean pair must have exactly one '+' (found $pluses)");
	}

	#@ Boolean pair has an empty word
	#@     xo_emit("{B:enabled/+off%d}", enabled);
	#@ Should be:
	#@     xo_emit("{B:enabled/on+off%d}", enabled);
	#@ Both words must be non-empty.  At run time, libxo reports
	#@ a broken pair and displays "true" or "false" instead.
	elsif ($pair =~ /^\+|\+$/) {
	    error("boolean pair has an empty word");
	}
    }

    if (length($rest)) {
	my($ff, $after) = ($rest =~
		/^(%[^%diouxXDOUeEfFgGaAcCsSp]*[diouxXDOUeEfFgGaAcCsSp])(.*)$/s);
	return () unless $ff;	# count_args reports this

	if (length($after)) {
	    if ($after =~ /^(?:%%|[^%])*$/) {
		#@ Boolean format has text after the conversion spec
		#@     xo_emit("{B:enabled/%d units}", enabled);
		#@ Should be:
		#@     xo_emit("{B:enabled/%d}{U: units}", enabled);
		#@ A boolean format is a boolean pair followed by a
		#@ single conversion spec, with nothing after it.
		error("boolean format has text after the conversion spec");
	    } else {
		#@ Boolean format has more than one conversion spec
		#@     xo_emit("{B:enabled/%d%d}", enabled, other);
		#@ Should be:
		#@     xo_emit("{B:enabled/%d}{B:other/%d}", enabled, other);
		#@ A boolean field has a single value, so it takes
		#@ a single conversion spec.
		error("boolean format has more than one conversion spec");
	    }
	    return ();
	}
	$spec = $ff;
    }

    #@ Boolean encoding format needs both a boolean pair and a conversion spec
    #@     xo_emit("{B,full-boolean:enabled/yes+no%d/%d}", enabled);
    #@ Should be:
    #@     xo_emit("{B,full-boolean:enabled/yes+no%d/1+0%d}", enabled);
    #@ The encoding format exists to give the encoded words, and
    #@ must say how the value is taken.
    if ($complete && (!length($pair) || !length($spec))) {
	error("boolean encoding format needs both a boolean pair "
	      . "and a conversion spec");
	return ();
    }

    return (length($pair) > 0, $star, $spec);
}

#
# Reduce a conversion spec to the parts that decide its arguments:
# the stars, the length modifiers, and the conversion.  A missing
# spec is "%s".
#
sub boolean_spec_type {
    my($spec) = @_;

    $spec = "%s" unless length($spec);
    $spec =~ s/[-#+ 0-9.]//g;
    $spec =~ s/[di]$/d/;
    return $spec;
}

sub count_args {
    my($format) = @_;

    return -1 unless $format;

    my $in;
    my $count = 0;
    my($text, $ff, $fc, $rest);
    for ($in = $format; $in; $in = $rest) {
	($text, $ff, $fc, $rest) =
	   ($in =~ /^([^%]*)(%[^%diouxXDOUeEfFgGaAcCsSp]*)([diouxXDOUeEfFgGaAcCsSp])(.*)$/);
	unless ($ff) {
	    # Might be a "%%"
	    ($text, $ff, $rest) = ($in =~ /^([^%]*)(%%)(.*)$/);
	    if ($ff) {
		check_text($text);
	    } else {
		# Not sure what's going on here, but something's wrong...
		error("invalid field format") if $in =~ /%/;
	    }
	    next;
	}

	check_text($text);
	check_field_format($ff, $fc);

	# Each "*" takes its width or precision from an argument
	$count += 1 + ($ff =~ tr/*//);
    }

    return $count;
}

sub check_field_format {
    my($ff, $fc) = @_;

    print "check_field_format: [$ff] [$fc]\n" if $opt_debug;

    my(@chunks) = split(/\./, $ff);

    #@ Max width only valid for strings
    #@     xo_emit("{:tag/%2.4.6d}", 55);
    #@ Should be:
    #@     xo_emit("{:tag/%2.6d}", 55);
    #@ libxo allows a true 'max width' in addition to the traditional
    #@ printf-style 'max number of bytes to use for input'.  But this
    #@ is supported only for string values, since it makes no sense
    #@ for non-strings.  This error may occur from a typo,
    #@ like "{:tag/%6..6d}" where only one period should be used.
    error("max width only valid for strings")
	if $#chunks >= 2 && $fc !~ /[sS]/;
}

#
# Lexicon (-L) support: count each tag that appears in the encoded
# output.  Roles other than value and boolean make no tag, and neither
# does a display-only ({d:}) field.  Names we can't see (from {a:}
# fields or non-literal arguments) are reported, not guessed.
#
sub lexicon_field {
    my($mods, $name) = @_;

    return if $mods =~ /[CDEFGLNPTUW\[\]]/;
    return if $mods =~ /d/;

    if ($mods =~ /a/) {
	lexicon_note("field name comes from an argument");
	return;
    }

    # Same rule as the "invalid character" error; a bad name is a
    # bug for lint to report, not a word for the lexicon
    if ($name !~ /^[0-9a-z-]*$/) {
	lexicon_note("invalid field name ($name)");
	return;
    }

    $lexicon{$name} += 1 if $name ne "";
}

sub lexicon_call {
    # parse_tokens() works from $', so it must run before any other match
    my @tokens = lexicon_parse_tokens();
    return unless @tokens;
    my($name) = grep { /^".*"$/ } @tokens;

    if (!defined($name)) {
	lexicon_note("name is not a string literal");
	return;
    }

    $name =~ s/^"//;
    $name =~ s/"$//;
    $lexicon{$name} += 1 if $name ne "";
}

#
# parse_tokens() dies when it can't find the end of a call.  When
# building a lexicon, that call is reported and skipped, so one odd
# line doesn't lose the rest of the file.  Otherwise, behave as before.
#
sub lexicon_parse_tokens {
    return parse_tokens() unless $opt_lexicon;

    my($save_ln, $save_curln) = ($ln, $curln);
    my @tokens = eval { parse_tokens() };
    if ($@) {
	($ln, $curln) = ($save_ln, $save_curln);
	lexicon_note("cannot find the end of this call");
	return ();
    }
    return @tokens;
}

sub lexicon_note {
    print STDERR $curfile . ": " . $curln . ": lexicon: "
	. join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
}

sub error {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": error: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $errors += 1;
}

sub warning {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": warning: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $warnings += 1;
}

sub info {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": info: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $info += 1;
}

main: {
    main();
}
