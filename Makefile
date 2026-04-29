APP_NAME := auto-low-power-listener
BUILD_DIR := build
SRC := src/auto_low_power_listener.c
OUT := $(BUILD_DIR)/$(APP_NAME)

.PHONY: build clean

build:
	mkdir -p $(BUILD_DIR)
	clang -O2 -Wall -Wextra -framework CoreFoundation -framework IOKit -o $(OUT) $(SRC)

clean:
	rm -rf $(BUILD_DIR)
