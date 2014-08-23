#!/usr/bin/env perl
#
# Copyright (c) 2014, Juniper Networks, Inc.
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

sub main {
    while ($ARGV[0] =~ /^-/) {
	$_ = shift @ARGV;
	$opt_cpp = 1 if /^-c/;
	$opt_cflags .= shift @ARGV if /^-C/;
	$opt_debug = 1 if /^-d/;
	$opt_text = 1 if /^-t/;
	extract_samples() if /^-X/;
    }

    for $file (@ARGV) {
	parse_file($file);
    }
}

sub extract_samples {
    my $x = "#@";
    my $cmd = "grep -B1 '$x Should be' $0 | grep xo_emit | sed 's/.\#*\@//'";
    system($cmd);
    exit(0);
}

sub parse_file {
    local($file) = @_;
    local($errors, $warnings, $info) = (0, 0, 0);
    local $curfile = $file;
    local $curln = 0;

    if ($opt_cpp) {
	open INPUT, "cpp $opt_cflags $file |";
    } else {
	open INPUT, $file || die "cannot open input file '$file'";
    }
    local @input = <INPUT>;
    close INPUT;

    local $ln, $rln, $line, $replay;

    for ($ln = 0; $ln < $#input; $ln++) {
	$line = $input[$ln];
	$curln += 1;

	if ($line =~ /^\#/) {
	    my($num, $fn) = ($line =~ /\#\s*(\d+)\s+"(.+)"/);
	    ($curfile, $curln) = ($fn, $num) if $num;
	    next;
	}

	next unless $line =~ /xo_emit\(/;

	@tokens = parse_tokens();
	print "token:\n    '" . join("'\n    '", @tokens) . "'\n"
	    if $opt_debug;
	check_format($tokens[0]);
    }

    print $file . ": $errors errors, $warnings warnings, $info info\n";
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
    if ($ln + 1 < $#input) {
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
	    $off += 1 if $ch eq "\\"; # double backslash: "\\/"
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
	    if ($ch eq "{") {
		check_text($build[0]) if length($build[0]);
		$braces = 1;
		@build = ();
		$last = $prev;
		next;
	    }
	}

	$prev = $ch;
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

    #@ A percent sign in text is a literal
    #@     xo_emit("cost: %d", cost);
    #@ Should be:
    #@     xo_emit("{L:cost}: {:cost/%d}", cost);
    #@ This can be a bit surprising and could be a missed field.
    info("a percent sign in text is a literal") if $text =~ /%/;
}

sub check_field {
    my(@field) = @_;
    print "checking field: [" . join("][", @field) . "]\n" if $opt_debug;

    #@ Last character before field definition is a field type
    #@ A common typo:
    #@     xo_emit("{T:Min} T{:Max}");
    #@ Should be:
    #@     xo_emit("{T:Min} {T:Max}");
    #@
    info("last character before field definition is a field type ($last)")
	if $last =~ /[DELNPTUVW\[\]]/ && $field[0] !~ /[DELNPTUVW\[\]]/;

    #@ Encoding format uses different number of arguments
    #@     xo_emit("{:name/%6.6s %%04d/%s}", name, number);
    #@ Should be:
    #@     xo_emit("{:name/%6.6s %04d/%s-%d}", name, number);
    #@ Both format should consume the same number of arguments off the stack
    my $cf = count_args($field[2]);
    my $ce = count_args($field[3]);
    warn("encoding format uses different number of arguments ($cf/$ce)")
	if $ce >= 0 && $cf >= 0 && $ce != $cf;

    #@ Only one field role can be used
    #@     xo_emit("{LT:Max}");
    #@ Should be:
    #@     xo_emit("{T:Max}");
    my(@roles) = ($field[0] !~ /([DELNPTUVW\[\]]).*([DELNPTUVW\[\]])/);
    error("only one field role can be used (" . join(", ", @roles) . ")")
	if $#roles > 0;

    # Field is a note, label, or title
    if ($field[0] =~ /[DLNT]/) {

	#@ Potential missing slash after N, L, or T with format
	#@     xo_emit("{T:%6.6s}\n", "Max");
	#@ should be:
	#@     xo_emit("{T/:%6.6s}\n", "Max");
	#@ The "%6.6s" will be a literal, not a field format
	info("potential missing slash after N, L, or T with format")
	    if $field[1] =~ /%/;

	#@ Format cannot be given when content is present (roles: DNLT)
	#@    xo_emit("{T:Max/%6.6s}", "Max");
	#@ Can't have both literal content and a format
	error("format cannot be given when content is present")
	    if $field[1] && $field[2];

	#@ An encoding format cannot be given (roles: DNLT)
	#@    xo_emit("{T:Max//%s}", "Max");
	#@ These fields are not emitted in the 'encoding' style (JSON, XML)
	error("encoding format cannot be given when content is present")
	    if $field[3];

	#@ An encoding format cannot be given (roles: DNLT)
	#@    xo_emit("{T:Max//%s}", "Max");
	#@ These fields are not emitted in the 'encoding' style (JSON, XML)
	error("encoding format cannot be given when content is present")
	    if $field[3];
    }

    # A value field
    if (length($field[0]) == 0 || $field[0] =~ /V/) {

	#@ Value field must have a name (as content)")
	#@     xo_emit("{:/%s}", "value");
	#@ Should be:
	#@     xo_emit("{:tag-name/%s}", "value");
	#@ The field name is used for XML and JSON encodings
	error("value field must have a name (as content)")
	    unless $field[1];

	#@ Use dashes, not underscores, for value field name
	#@     xo_emit("{:no_under_scores}", "bad");
	#@ Should be:
	#@     xo_emit("{:no-under-scores}", "bad");
	error("use dashes, not underscores, for value field name")
	    if $field[1] =~ /_/;

	#@ Value field name cannot start with digit
	#@     xo_emit("{:3com/}");
	#@ Should be:
	#@     xo_emit("{:x3com/}");
	error("value field name cannot start with digit")
	    if $field[1] =~ /^[0-9]/;

	#@ Value field name should be lower case
	#@     xo_emit("{:WHY-ARE-YOU-SHOUTING}", "NO REASON");
	#@ Should be:
	#@     xo_emit("{:why-are-you-shouting}", "NO REASON");
	error("value field name should be lower case")
	    if $field[1] =~ /[A-Z]/;

	#@ Value field name contains invalid character
	#@     xo_emit("{:cost-in-$$/%u}", 15);
	#@ Should be:
	#@     xo_emit("{:cost-in-dollars/%u}", 15);
	error("value field name contains invalid character (" . $field[1] . ")")
	    unless $field[1] =~ /^[0-9a-z-]*$/;
    }

    # A decoration field
    if ($field[0] =~ /D/) {

	#@decoration field contains invalid character
	#@     xo_emit("{D:not good}");
	#@ Should be:
	#@     xo_emit("{D:((}{:good}{D:))}", "yes");
	#@ This is minor, but fields should use proper roles.
	warn("decoration field contains invalid character")
	    unless $field[1] =~ m:^[~!\@\#\$%^&\*\(\);\:\[\]\{\} ]+$:;
    }

    if ($field[0] =~ /[\[\]]/) {
	#@ Anchor content should be decimal width
	#@     xo_emit("{[:mumble}");
	#@ Should be:
	#@     xo_emit("{[:32}");
	error("anchor content should be decimal width")
	    if $field[1] && $field[1] !~ /^-?\d+$/ ;

	#@ Anchor format should be "%d"
	#@     xo_emit("{[:/%s}");
	#@ Should be:
	#@     xo_emit("{[:/%d}");
	error("anchor format should be \"%d\"")
	    if $field[2] && $field[2] ne "%d";
    }
}

sub count_args {
    my($format) = @_;

    return -1 unless $format;

    my $in;
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
    }

    return 0;
}

sub check_field_format {
    my($ff, $fc) = @_;

    print "check_field_format: [$ff] [$fc]\n" if $opt_debug;

    my(@chunks) = split(/\./, $ff);

    #@ Max width only valid for strings
    #@     xo_emit("{:tag/%2.4.6d}", 55);
    #@ Should be:
    #@     xo_emit("{:tag/%2.6d}", 55);
    error("max width only valid for strings")
	if $#chunks >= 2 && $fc =~ /[sS]/;
}

sub error {
    print STDERR $curfile . ": " .$curln . ": error: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_text;
    $errors += 1;
}

sub warn {
    print STDERR $curfile . ": " .$curln . ": warning: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_text;
    $warnings += 1;
}

sub info {
    print STDERR $curfile . ": " .$curln . ": info: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_text;
    $info += 1;
}

main: {
    main();
}
