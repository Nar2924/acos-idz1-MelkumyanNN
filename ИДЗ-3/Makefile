CC = gcc-15
CFLAGS = -Wall -Wextra -std=gnu11 -g
LDFLAGS = -lrt -pthread

COMMON_SRCS = common/log.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

GARDEN_SRCS = garden/main.c garden/garden.c
GARDEN_OBJS = $(GARDEN_SRCS:.c=.o)

GARDENER_SRCS = gardener/main.c gardener/gardener_logic.c
GARDENER_OBJS = $(GARDENER_SRCS:.c=.o)

all: garden_app gardener_app

garden_app: $(GARDEN_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $(GARDEN_OBJS) $(COMMON_OBJS) $(LDFLAGS)

gardener_app: $(GARDENER_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $(GARDENER_OBJS) $(COMMON_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(GARDEN_OBJS) $(GARDENER_OBJS) $(COMMON_OBJS) garden_app gardener_app simulation.log

.PHONY: all clean
