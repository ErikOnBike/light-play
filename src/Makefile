# Simple makefile for light-play

CC=gcc
CFLAGS=-Wall -O -c
LIBS=-lpthread
OBJS=light-play.o \
	m4afile.o \
	raopclient.o \
	rtspclient.o \
	rtsprequest.o \
	rtspresponse.o \
	network.o \
	buffer.o \
	log.o \
	utils.o \
	md5/md5.o

all: light-play

clean:
	rm light-play $(OBJS)

light-play: $(OBJS)
	$(CC) -o light-play $(OBJS) $(LIBS)

md5/md5.o:
	$(CC) $(CFLAGS) md5/md5.c -o md5/md5.o

%.o: %.c
	$(CC) $(CFLAGS) $<
