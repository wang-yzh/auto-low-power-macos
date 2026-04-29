APP_NAME := auto-low-power-listener
BUILD_DIR := build
DIST_DIR := dist
SRC := src/auto_low_power_listener.c
OUT := $(BUILD_DIR)/$(APP_NAME)
VERSION ?= dev
ARCHIVE := $(DIST_DIR)/auto-low-power-macos-$(VERSION).tar.gz

.PHONY: build package clean

build:
	mkdir -p $(BUILD_DIR)
	clang -O2 -Wall -Wextra -framework CoreFoundation -framework IOKit -o $(OUT) $(SRC)

package:
	mkdir -p $(DIST_DIR)
	git archive --format=tar.gz --worktree-attributes --prefix=auto-low-power-macos/ -o $(ARCHIVE) HEAD

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
