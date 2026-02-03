# Makefile for Blooming Sunflower project
# builds:
#   garden_server  - the main controller
#   flower_client  - one flower in the garden

CC      = gcc
CFLAGS = -Wall -Wextra -g -Wno-sign-compare -Wno-type-limits
LDFLAGS = -pthread

SERVER_OBJS = garden_server.o csapp.o
CLIENT_OBJS = flower_client.o flower.o csapp.o

all: garden_server flower_client

garden_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o garden_server $(SERVER_OBJS) $(LDFLAGS)

flower_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o flower_client $(CLIENT_OBJS) $(LDFLAGS)

# generic rule for .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o garden_server flower_client