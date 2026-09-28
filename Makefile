ARCH=aarch64-linux-gnu-

CC := $(ARCH)gcc

# Binary name
TARGET := ESPX

# Source and Object files
SRCS := main.c
OBJS := $(SRCS:.c=.o)

# Compiler flags
CFLAGS  := -Wall -Wextra -O2 -D_GNU_SOURCE

# Linker flags for libraries link
LDFLAGS := -lwebsockets -lcjson -lpthread -lrt -lm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)