CC=gcc
CFLAGS=-g

.PHONY: all
all: rle_encoder

nyuenc: rle_encoder.o

nyuenc.o: rle_encoder.c

.PHONY: clean
clean:
	rm -f *.o rle_encoder