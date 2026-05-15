CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -D_GNU_SOURCE
CPPFLAGS ?= -I/usr/include/postgresql
LDLIBS   = -lpq -lhiredis

SRC_DIR := src
TEST_DIR := tests

APP_SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/handlers.c $(SRC_DIR)/db.c $(SRC_DIR)/cache.c $(SRC_DIR)/vendor/mongoose.c
APP_OBJS := $(APP_SRCS:.c=.o)

TEST_SRCS := $(TEST_DIR)/test_handlers.c $(SRC_DIR)/handlers.c $(SRC_DIR)/cache.c $(SRC_DIR)/db.c $(TEST_DIR)/vendor/unity.c
# Tests link handlers/cache/db/unity + mongoose for symbol resolution; no main.o.

.PHONY: all clean test

all: server

server: $(APP_OBJS)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

# handlers.c includes mongoose.h. For tests we need handlers.c to compile
# but tests only exercise pure helpers — link mongoose.o in too for symbol
# resolution of any mongoose functions referenced (mg_http_reply, etc.).
test_handlers: $(TEST_SRCS) $(SRC_DIR)/vendor/mongoose.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -I$(SRC_DIR) -o $@ $(TEST_SRCS) $(SRC_DIR)/vendor/mongoose.c -lhiredis -lpq -lpthread

test: test_handlers
	./test_handlers

clean:
	rm -f server test_handlers $(APP_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
