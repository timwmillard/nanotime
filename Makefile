
CFLAGS += -g

all: timetest

timetest: main.c nanotime.h
	clang $(CFLAGS) main.c -o timetest

gen: nanotime.h

nanotime.h: gen.sh src/time.c src/time.h
	sh gen.sh > nanotime.h

clean:
	rm -f nanotime.h
	rm -f timetest

