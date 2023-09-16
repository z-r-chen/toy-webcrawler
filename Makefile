PROGS = crawler

# Runtime Environment: macOS Catalina Version 10.15.4 (19E287)

# Usage:
# make && ./crawler comp3310.ddns.net 7880 && make clean

all: $(PROGS)

%: %.c
	gcc -Wall -o $* $*.c
clean:
	rm -f $(PROGS) *.class