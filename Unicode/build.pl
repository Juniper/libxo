#
# Build the tolower and toupper functions
#
# use strict;
use warnings;
use utf8;

open(DATA, "<UnicodeData.txt") || die;

#my %ucase = ();
#my %lcase = ();
#my %delta = ();
#my %counts = ();
#my %list = ();

my %map_f12 = ();
my %ucase_f12 = ();
my %lcase_f12 = ();
my %delta_f12 = ();
my %counts_f12 = ();
my %list_f12 = ();

my %map_f13 = ();
my %ucase_f13 = ();
my %lcase_f13 = ();
my %delta_f13 = ();
my %counts_f13 = ();
my %list_f13 = ();

my %names = ();
my @name_stack = ();

my $indent = "    ";
my $hdr_if = "if (";
my $hdr_else = "} else if (";
my $hdr_or = "        || ";
my $hdr_then = ") {";
my $header = $hdr_if;

my $debug;

sub load_data {
    foreach (<DATA>) {
	my @f = split(/;/);

	my $ch = eval "0x${f[0]}";

	if ($f[12]) {

	    my $uc = eval "0x${f[12]}";
	    my $lc = $ch;
	    my($delta);
	    $delta = $lc - $uc;

	    $counts_f12{$delta} += 1;
	    if (defined($list_f12{$delta})) {
		push(@{$list_f12{$delta}}, $ch);
	    } else {
		$list_f12{$delta} = [ $ch ];
	    }

	    $map_f12{$ch} = $uc;
	    $ucase_f12{$lc} = $uc;
	    $lcase_f12{$uc} = $lc;
	    $delta_f12{$ch} = $delta;

	    $names{$ch} = $f[1];
	}

	if ($f[13]) {
	    my $lc = eval "0x${f[13]}";
	    my $uc = $ch;

	    my($delta);
	    $delta = $uc - $lc;

	    $counts_f13{$delta} += 1;
	    if (defined($list_f13{$delta})) {
		push(@{$list_f13{$delta}}, $ch);
	    } else {
		$list_f13{$delta} = [ $ch ];
	    }

	    $map_f13{$ch} = $lc;
	    $ucase_f13{$lc} = $uc;
	    $lcase_f13{$uc} = $lc;
	    $delta_f13{$lc} = $delta;
	    
	    $names{$ch} = $f[1];
	}
    }
}

sub dump {
    for my $lc (keys(%ucase_f12)) {
	my ($uc, $delta) = ($ucase_f12{$lc}, $delta_f12{$lc});
	printf("%04x %04x %04x/%04x\n", $uc, $lc, $delta, abs($delta)) if $debug;
    }

    if ($debug) {
	printf("keys:\n");
	for my $k (sort { $counts_f12{$b} <=> $counts_f12{$a} } keys(%counts_f12)) {
	    printf("%04x/%d %u\n", $k, $k, $counts_f12{$k}) unless $counts_f12{$k} < 5;
	}
    }
}

sub gen_range {
    my($d, $k, $fn, $test) = @_;

    my($first, $last) = (0, 0);

    my $comp = 1;
    $comp = 2 if $d == 1;

    for (my $i = 0; $i <= $#{$list{$k}}; $i++) {
	my $ch = $list{$k}->[$i];

	unless ($first) {
	    $first = $ch;
	    $last = $ch;
	    push(@name_stack, $names{$ch});
	    next;
	}

	if ($test) {
	    next if $test->($ch, $d, $k);
	}

	if ($last && $ch != $last + $comp) {
	    if ($first == $last) {
		printf("\n%s%s(wc == %#06x)",
		       $indent, $header, $first);
	    } else {
		printf("\n%s%s(%#06x <= wc && wc <= %#06x)",
		       $indent, $header, $first, $last);
	    }

	    $first = $ch;
	    push(@name_stack, $names{$ch});
	    $header = $hdr_or;
	}

	$last = $ch;
    }

    if ($first && $last) {
	if ($first == $last) {
	    printf("\n%s%s(wc == %#06x)%s",
		   $indent, $header, $first, $hdr_then);
	} else {
	    printf("\n%s%s(%#06x <= wc && wc <= %#06x)%s\n",
		   $indent, $header, $first, $last, $hdr_then);
	}
    } else {
	printf("%s\n", $hdr_then);
    }

    while (my $n = shift @name_stack) {
	printf("%s%s/* %s */\n", $indent, $indent, $n);
    }

    if ($fn) {
	$fn->($d, $k);
    } else {
	if ($k > 0) {
	    printf("%s%swc -= %#06x;\n", $indent, $indent, $d);
	} else {
	    printf("%s%swc += %#06x;\n", $indent, $indent, $d);
	}
    }

    $header = $hdr_else;
}

sub turn_on_plus_low_bit {
    my($d, $k) = @_;

    printf("%s%swc |= 1;\n", $indent, $indent);
}

sub turn_off_plus_low_bit {
    my($d, $k) = @_;

    printf("%s%swc += 1;\n", $indent, $indent);
    printf("%s%swc &= ~1;\n", $indent, $indent);
}

sub turn_off_minus_low_bit {
    my($d, $k) = @_;

    printf("%s%swc &= ~1;\n", $indent, $indent);
}

sub turn_on_minus_low_bit {
    my($d, $k) = @_;

    printf("%s%swc -= 1;\n", $indent, $indent);
    printf("%s%swc |= 1;\n", $indent, $indent);
}

sub test_even {
    my($i) = @_;
    return (($i & 1) == 0);
}

sub test_odd {
    my($i) = @_;
    return (($i & 1) != 0);
}

sub generate {
    my($name, $k1) = @_;

    printf("\nwchar_t\n%s (wchar_t wc)\n{\n", $name);

    if (0) {
	gen_range(0x20, 0x20);
	gen_range(0x50, 0x50);
	gen_range(0x1a, 0x1a);
	gen_range(0x30, 0x30);
	gen_range(1, 1, \&turn_off_minus_low_bit, \&test_even);
	gen_range(1, 1, \&turn_on_minus_low_bit, \&test_odd);
	return;
    }

    my @ones;

    if ($k1 == 1) {
	# build_toupper
	gen_range(1, $k1, \&turn_off_minus_low_bit, \&test_even);
	gen_range(1, $k1, \&turn_on_minus_low_bit, \&test_odd);
    } else {
	# build_tolower
	gen_range(1, $k1, \&turn_on_plus_low_bit, \&test_odd);
	gen_range(1, $k1, \&turn_off_plus_low_bit, \&test_even);
    }

    for my $k (sort { $counts{$b} <=> $counts{$a} } keys(%counts)) {
	my $d = abs($k);
	my $c = $#{$list{$k}} + 1;
	if ($d == 1) {

	} elsif ($counts{$k} >= 5) {
	    gen_range($d, $k);
	} else {
	    for (my $i = 0; $i <= $#{$list{$k}}; $i++) {
		my $ch = $list{$k}->[$i];
		push(@ones, $ch);
	    }
	}
    }

    printf("\n    } else {\n        switch (wc) {\n");

    for my $ch (sort {$a <=> $b} @ones) {
	my $n = $names{$ch};
	$n = "" unless $n;

	printf("        case %#06x: wc = %#06x; break; /* %s */\n",
	       $ch, $map{$ch}, $n);
    }
    printf("        }\n    }\n\n    return wc;\n}\n\n");
}

sub gen_test {
    my ($name, $me) = @_;

    my $func = <<EndOfFunc;
void
$me (void)
{
    wchar_t wc;
    for (wchar_t i = 1; i < 0x1f000; i++) {
	wc = $name(i);
	if (i != wc) {
	    printf("%#06x %#06x\\n", i, wc);
	}
    }
}
EndOfFunc

    CORE::say $func;
}

sub gen_main {
    my($name) = @_;

    my $main = <<EndOfMain;
int
main (void)
{
    $name();
}

EndOfMain

    CORE::say $main;
}

sub build_toupper {
    local %map = %map_f12;
    local %ucase = %ucase_f12;
    local %lcase = %lcase_f12;
    local %delta = %delta_f12;
    local %counts = %counts_f12;
    local %list = %list_f12;

    printf("#include <stdio.h>\n");
    printf("#include <wchar.h>\n");

    &generate("xo_utf8_wtoupper", 1);
    &gen_test("xo_utf8_wtoupper", "test_xo_utf8_wtoupper");

    &gen_main("test_xo_utf8_wtoupper");
}

sub build_tolower {
    local %map = %map_f13;
    local %ucase = %ucase_f13;
    local %lcase = %lcase_f13;
    local %delta = %delta_f13;
    local %counts = %counts_f13;
    local %list = %list_f13;

    printf("#include <stdio.h>\n");
    printf("#include <wchar.h>\n");

    &generate("xo_utf8_wtolower", -1);
    &gen_test("xo_utf8_wtolower", "test_xo_utf8_wtolower");

    &gen_main("test_xo_utf8_wtolower");
}

sub build_lower {
    for my $uc (sort { $a <=> $b } keys(%map_f13)) {
	my $lc = $map_f13{$uc};
	printf("%#06x %#06x\n", $uc, $lc);
    }
}

sub build_upper {
    for my $lc (sort { $a <=> $b } keys(%map_f12)) {
	my $uc = $map_f12{$lc};
	printf("%#06x %#06x\n", $lc, $uc);
    }
}

sub build_delta {
    for my $lc (sort { $a <=> $b } keys(%ucase_f13)) {
	my ($uc, $delta) = ($ucase_f13{$lc}, $delta_f12{$lc});
	next unless $delta;

	my $count = $counts_f12{$delta};
	my $minus = "";
	if ($delta < 0) {
	    $minus = "-";
	    $delta *= -1;
	}
	printf("%#06x %#06x %s%#06x/%#06x (%d)\n",
	       $lc, $uc, $minus, $delta, abs($delta), $count);
    }
}

# These are asymetric loops, where upper(lower(x)) != x
# These are about 36 such cases (as of 2023/05/15)
sub build_bad {
    my %done = ();
    
    printf("lower:\n");
    for my $lc (sort { $a <=> $b } keys(%ucase_f12)) {
	my $uc = $ucase_f12{$lc};
	my $cc = $lcase_f13{$uc};
	if ($cc && $cc != $lc) {
	    printf("%#06x -> %#06x -> %#06x\n",
		   $lc, $uc, $cc);
	}
    }

    printf("upper:\n");
    for my $uc (sort { $a <=> $b } keys(%lcase_f13)) {
	my $lc = $lcase_f13{$uc};
	my $cc = $ucase_f12{$lc};
	if ($cc && $cc != $uc) {
	    printf("%#06x -> %#06x -> %#06x\n",
		   $uc, $lc, $cc);
	}
    }
}

main: {
    &load_data();
    &dump();

    my $job = shift @ARGV;

    $job = "code" unless $job;
    $job = "build_" . $job;

    {
	no strict "refs";
	&$job();
    }
}
