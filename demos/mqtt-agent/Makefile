CC = gcc

INCLUDE_DIRS = -I . \
               -I ./agent_helper/ \
               -I ../../libraries/standard/coreMQTT/source/include \
               -I ../../libraries/standard/coreMQTT/source/interface \
               -I ../../libraries/standard/backoffAlgorithm/source/include \
               -I ../../platform/include \
               -I ../../platform/posix/transport/include \
               -I ../logging-stack/ \
	       -I ./mqtt_demo/

SRC_FILES = mqtt_agent_demo.c \
            demo_queue.c \
            $(shell find ../../libraries/standard/coreMQTT/source -name '*.c') \
            $(shell find ../../libraries/standard/backoffAlgorithm/source -name '*.c') \
            ../../platform/posix/transport/src/sockets_posix.c \
            ../../platform/posix/transport/src/plaintext_posix.c \
            ../../platform/posix/transport/src/openssl_posix.c \
            ../../platform/posix/clock_posix.c

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

LDFLAGS = -L/usr/local/opt/openssl@1.1/lib -lssl -lcrypto

all: mqtt_agent.o mqtt_agent_helper.o mqtt_demo_plaintext.o mqtt_demo_mutual_auth.o
#	$(CC) $(FLAGS) $(LDFLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -o mqtt_agent_demo
	$(CC) $(FLAGS) $(LDFLAGS) $(INCLUDE_DIRS) $(SRC_FILES) $^ -o mqtt_agent_demo

mqtt_agent.o: mqtt_agent.c mqtt_agent.h mqtt_agent_config.h
	$(CC) $(FLAGS) $(INCLUDE_DIRS) mqtt_agent.c -c -o $@

mqtt_agent_helper.o: agent_helper/mqtt_agent_helper.c agent_helper/mqtt_agent_helper.h
	$(CC) -undefined dynamic_lookup $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

mqtt_demo_plaintext.o: mqtt_demo/mqtt_demo_plaintext.c
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

mqtt_demo_mutual_auth.o: mqtt_demo/mqtt_demo_mutual_auth.c
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

clean:
	rm -rf mqtt_agent_demo mqtt_agent_demo.dSYM *.o
