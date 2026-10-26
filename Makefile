
CC = cc
CFLAGS = -O2 -Wall -Wextra
LDLIBS = -lX11 -l:libXft.so.2 -l:libXrender.so.1

.PHONY: all clean size

all: taskmgr

taskmgr: taskmgr.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f taskmgr

size: taskmgr
	@printf 'Executable size: '
	@stat -c '%s bytes' taskmgr
