.PHONY: clean

STRIP = strip

CFLAGS = -O3 -Wall -Werror

echor: main.o
	$(CC) $(CFLAGS) -o $@ $+
	$(STRIP) $@

clean:
	rm -f echor *.o
