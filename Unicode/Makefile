#
# 
#

CFLAGS=-g

all: xo_tolower xo_toupper xo_tolower files results

files: data-lower.txt data-upper.txt delta.txt out-lower.txt out-upper.txt

xo_tolower: xo_tolower.c
	cc ${CFLAGS} -o $@ xo_tolower.c

xo_tolower.c: build.pl
	perl build.pl tolower > $@

xo_toupper: xo_toupper.c
	cc ${CFLAGS} -o $@ xo_toupper.c

xo_toupper.c: build.pl
	perl build.pl toupper > $@

delta.txt: build.pl
	perl build.pl delta > delta.txt

data-lower.txt: build.pl
	perl build.pl lower | sort > data-lower.txt

data-upper.txt: build.pl
	perl build.pl upper | sort > data-upper.txt

out-lower.txt: xo_tolower
	./xo_tolower lower | sort > out-lower.txt

out-upper.txt: xo_toupper
	./xo_toupper upper | sort > out-upper.txt
 
lldb gdb: xo_toupper xo_tolower
	lldb xo_toupper

clean:
	rm -f builder.c xo_tolower.c xo_toupper.c
	rm -f data-lower.txt data-upper.txt
	rm -f out-lower.txt out-upper.txt


TEST=\
cmp -s data-$$CASE.txt out-$$CASE.txt; \
if [ $$? -ne 0 ]; then echo "$$CASE case fails"; \
else echo "$$CASE case succeeds"; \
fi

results:
	@CASE=lower; ${TEST}
	@CASE=upper; ${TEST}

