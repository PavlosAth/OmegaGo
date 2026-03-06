## Short makefile to compile my Go engine

## You SHOULD NOT modify the parameters below

## Compiler to use - do not change
CC = gcc

## Compiler flags - do not change
CFLAGS = -Wall -Wextra -Werror -pedantic -O3

## Linking flags - do not change
LDFLAGS = $(CFLAGS)

## The name of the binary (executable) file - do not change
TARGET ?= goteam

## Build the target by default
all: $(TARGET)

## You can change everything below to make your target compile,
## link, clean or do anything else you'd like.

EXTRA_CFLAGS = -flto -ffast-math
EXTRA_LDFLAGS = -flto 

OBJS = src/goteam.o src/playMove.o src/genMove.o src/finalScore.o

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(EXTRA_LDFLAGS) $(OBJS) -lm 

%.o: %.c goteam.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@ 
