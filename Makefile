CC = gcc
CFLAGS = -Wall -O2

all: vaxp-session

vaxp-session: src/vaxp-session.c
	$(CC) $(CFLAGS) -o vaxp-session src/vaxp-session.c

clean:
	rm -f vaxp-session
