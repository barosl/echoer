.PHONY: clean

STRIP = strip

CFLAGS = -O3 -Wall -Werror

echoer: main.o
	$(CC) $(CFLAGS) -o $@ $+
	$(STRIP) $@

clean:
	rm -f echoer *.o
