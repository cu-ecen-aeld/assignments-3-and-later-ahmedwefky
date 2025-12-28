# Cross-compiler settings
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -Wall -Wextra -Werror
LDFLAGS =

# Target
TARGET = aesdsocket
SOURCES = aesdsocket.c
OBJECTS = $(SOURCES:.c=.o)

# Default target
.PHONY: all default clean

all: default

default: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)