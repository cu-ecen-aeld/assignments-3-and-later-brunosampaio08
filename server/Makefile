CC ?= $(CROSS_COMPILE)gcc
CCFLAGS += -g -Wall -Werror

aesdsocket:# | server-dir
	$(CC) $(CCFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.c

#server-dir:
#	mkdir -p ../server

all: aesdsocker

clean:
	rm -f aesdsocket
