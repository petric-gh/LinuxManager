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
	@if [ "$$(stat -c '%s' taskmgr)" -lt 1000000 ]; then \
		echo 'Under 1 MB: YES'; \
	else \
		echo 'Under 1 MB: NO'; \
	fi
