CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

SRCS := writer.c
OBJS := $(SRCS:.c=.o)
TARGET := writer

CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)