
all: timetest

# timetest: main.c
# 	clang main.c -o timetest

timetest: main.o nanotime.o
	clang main.o ./nanotime.o -o timetest

main.o: main.c
	clang -c main.c

nanotime.o: nanotime.h
	clang -D NANOTIME_IMPLEMENTATION -c nanotime.h

# timetest: main.c src/time.c
# 	clang main.c src/time.c -o timetest

gen: nanotime.h

nanotime.h: gen.sh src/time.c src/time.h
	sh gen.sh > nanotime.h

# nanotime.h: src/time.h src/time.c
# 	cat src/time.h src/time.c > nanotime.h

clean:
	rm -f nanotime.o
	rm -f main.o
	rm -f nanotime.h

