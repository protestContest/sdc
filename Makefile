NAME = sdc

BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
LIB_DIR := lib
DIST_DIR := dist
PREFIX := $(HOME)/.local
TARGET = ./$(DIST_DIR)/$(NAME)
SHELL = bash

SRCS := $(shell find $(SRC_DIR) -name *.c -print)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CC = clang
INCLUDE_FLAGS = -I$(INC_DIR) -include $(INC_DIR)/base.h
WARN_FLAGS = -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-comment
CFLAGS = -std=c89 -g -O0 $(WARN_FLAGS) $(INCLUDE_FLAGS)
LDFLAGS =

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR)

.PHONY: test
test:
	$(TARGET) support/Test\ Frame.tiff
