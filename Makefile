CC = gcc
CFLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Iinclude # -Wpedantic
LDFLAGS = -lpthread

MODE ?= debug
ifeq ($(MODE),release)
	CFLAGS += -g -O2
else
	CFLAGS += -g -O0
endif

OBJ_DIR = build/$(MODE)
BIN_DIR = bin/$(MODE)
COMMON_OBJS := $(OBJ_DIR)/protocol.o $(OBJ_DIR)/transfer.o $(OBJ_DIR)/utils.o

SERVER_EXE := $(BIN_DIR)/server
CLIENT_EXE := $(BIN_DIR)/client

all: debug

debug:
	@$(MAKE) MODE=debug build_all
release:
	@$(MAKE) MODE=release build_all
build_all: $(SERVER_EXE) $(CLIENT_EXE)

$(SERVER_EXE): $(OBJ_DIR)/server.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)
$(CLIENT_EXE): $(OBJ_DIR)/client.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(LDFLAGS)
# 通用规则
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf build bin
.PHONY: all debug release clean build_all
