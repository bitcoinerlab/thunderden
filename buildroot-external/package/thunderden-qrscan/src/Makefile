CC ?= cc
CFLAGS ?= -O2
LDFLAGS ?=

all: thunderden-qrscan

thunderden-qrscan: thunderden-qrscan.c psbt_payload_decoder.c psbt_payload_decoder.h
	$(CC) $(CFLAGS) -Wall -Wextra -std=gnu11 -o $@ thunderden-qrscan.c psbt_payload_decoder.c $(LDFLAGS) -lzbar -lv4l2 -lv4lconvert -lz

clean:
	rm -f thunderden-qrscan

.PHONY: all clean
