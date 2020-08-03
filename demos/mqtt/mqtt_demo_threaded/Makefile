CC = gcc

INCLUDE_DIRS = -I . \
               -I ../../../libraries/standard/mqtt/include \
               -I ../../../platform/include \
               -I ../../../platform/posix/transport/include \
               -I ../../logging-stack/

SRC_FILES = mqtt_demo_threaded.c \
            demo_queue.c \
            $(shell find ../../../libraries/standard/mqtt/src -name '*.c') \
            ../../../platform/posix/transport/src/sockets_posix.c \
            ../../../platform/posix/transport/src/plaintext_posix.c \
            ../../../platform/posix/clock_posix.c \
            ../../../platform/posix/transport/src/transport_reconnect_posix.c

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

all:
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -o mqtt_demo_threaded

clean:
	rm -rf mqtt_demo_threaded mqtt_demo_threaded.dSYM
