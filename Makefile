CC = gcc

CFLAGS = -Wall -Wextra -Werror -pedantic -O3 -static
LDFLAGS = $(CFLAGS)

TARGET ?= goteam

all: $(TARGET)


EXTRA_CFLAGS = -flto -ffast-math
EXTRA_LDFLAGS = -flto 

OBJS = src/goteam.o src/playMove.o src/genMove.o src/finalScore.o

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(EXTRA_LDFLAGS) $(OBJS) -lm 

%.o: %.c goteam.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@ 
