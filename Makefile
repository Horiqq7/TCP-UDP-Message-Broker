CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lm

all: server subscriber

server: server.c
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

subscriber: subscriber.c
	$(CC) $(CFLAGS) subscriber.c -o subscriber $(LDFLAGS)

clean:
	rm -f server subscriber