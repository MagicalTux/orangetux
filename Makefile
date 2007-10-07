#!/bin/make

CC=gcc
OBJECTS=main.o serial.o modem.o
TARGET=orangetux
PACKAGES=gtk+-2.0

CFLAGS=-g -ggdb -Wall -pipe --std=gnu99 -fgnu89-inline $(shell pkg-config --cflags $(PACKAGES))
LIBS=$(shell pkg-config --libs $(PACKAGES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(TARGET) $(OBJECTS)

