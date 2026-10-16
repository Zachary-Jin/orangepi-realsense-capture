CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags realsense2)
LDLIBS += $(shell pkg-config --libs realsense2)

.PHONY: all clean

all: realsense_capture

realsense_capture: realsense_capture.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f realsense_capture
