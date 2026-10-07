CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -pthread
INCLUDES = -I./include
SRCS = src/main.c src/garden.c src/gardener.c src/log.c
OBJS = $(SRCS:.c=.o)
TARGET = flower_sim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
