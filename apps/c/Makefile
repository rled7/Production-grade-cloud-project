CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -D_GNU_SOURCE
CPPFLAGS ?= -I/usr/include/postgresql
LDLIBS   = -lpq -lhiredis -lhiredis_ssl -lcrypto -lssl -lcrypt

SRC_DIR  := src
TEST_DIR := tests

APP_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/handlers.c $(SRC_DIR)/db.c \
            $(SRC_DIR)/cache.c $(SRC_DIR)/auth.c $(SRC_DIR)/access_log.c \
            $(SRC_DIR)/vendor/mongoose.c
APP_OBJS := $(APP_SRCS:.c=.o)

TEST_SRCS := $(TEST_DIR)/test_handlers.c \
             $(SRC_DIR)/handlers.c $(SRC_DIR)/cache.c $(SRC_DIR)/db.c \
             $(SRC_DIR)/auth.c $(SRC_DIR)/access_log.c \
             $(TEST_DIR)/vendor/unity.c

.PHONY: all clean test

all: server

server: $(APP_OBJS)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

test_handlers: $(TEST_SRCS) $(SRC_DIR)/vendor/mongoose.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(SRC_DIR) -o $@ \
	    $(TEST_SRCS) $(SRC_DIR)/vendor/mongoose.c \
	    -lhiredis -lhiredis_ssl -lpq -lcrypto -lssl -lcrypt -lpthread

test: test_handlers
	./test_handlers

clean:
	rm -f server test_handlers $(APP_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
