
CFLAGS += -Wall -g

all: timetest

timetest: main.c nanotime.h
	clang $(CFLAGS) main.c -o timetest

nanotime.h: gen.sh src/time.c src/time.h
	sh gen.sh > nanotime.h

run: all
	./timetest


test: src/time_test
	src/time_test

src/time_test: src/time_test.c src/time.c src/time.h
	clang $(CFLAGS) src/time_test.c src/time.c -o src/time_test


clean:
	rm -f nanotime.h
	rm -f timetest
	rm -rf timetest.dSYM
	rm -f src/time_test
	rm -rf src/time_test.dSYM

