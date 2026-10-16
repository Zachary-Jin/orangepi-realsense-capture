CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags realsense2)
LDLIBS += $(shell pkg-config --libs realsense2)

.PHONY: all clean

all: realsense_capture realsense_web

realsense_capture: realsense_capture.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

realsense_web: realsense_web.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS) -pthread

clean:
	rm -f realsense_capture realsense_web
