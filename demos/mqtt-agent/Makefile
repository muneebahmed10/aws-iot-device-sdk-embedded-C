CC ?= gcc

INCLUDE_DIRS = -I ./coreMQTT-Agent/source/include \
               -I . \
               -I ./mqtt-agent-interface \
               -I ./subscription-manager \
               -I ./agent_helper/ \
               -I ../../libraries/standard/coreMQTT/source/include \
               -I ../../libraries/standard/coreMQTT/source/interface \
               -I ../../libraries/standard/backoffAlgorithm/source/include \
               -I ../../platform/include \
               -I ../../platform/posix/transport/include \
               -I ../logging-stack/ \
	       -I ./mqtt_demo/

SRC_FILES = mqtt_agent_demo.c \
            $(shell find ./mqtt-agent-interface -name '*.c') \
            $(shell find ./subscription-manager -name '*.c') \
            $(shell find ../../libraries/standard/coreMQTT/source -name '*.c') \
            $(shell find ../../libraries/standard/backoffAlgorithm/source -name '*.c') \
            ../../platform/posix/transport/src/sockets_posix.c \
            ../../platform/posix/transport/src/plaintext_posix.c \
            ../../platform/posix/transport/src/openssl_posix.c \
            ../../platform/posix/clock_posix.c

AGENT_SRCS = $(shell find ./coreMQTT-Agent/source -name '*.c')

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

LDFLAGS = -L/usr/local/opt/openssl@1.1/lib -lssl -lcrypto

OBJECTS := $(wildcard */*.o)
SHADOW_OBJECTS = shadow_demo/shadow.o \
		 shadow_demo/core_json.o \
		 shadow_demo/shadow_demo_helpers.o \
		 shadow_demo/shadow_demo_main.o
SHADOW_SRCS := $(wildcard shadow_demo/*.c)
DEFENDER_OBJECTS := $(wildcard defender_demo/*.o)
LD ?= ld

all: mqtt_agent.o mqtt_agent_command_functions.o mqtt_agent_helper.o mqtt_demo_plaintext.o mqtt_demo_mutual_auth.o shadow_demo.o
#	$(CC) $(FLAGS) $(LDFLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -o mqtt_agent_demo
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $(SRC_FILES) $^ $(LDFLAGS) -o mqtt_agent_demo

mqtt_agent.o: core_mqtt_config.h
	$(CC) $(FLAGS) $(INCLUDE_DIRS) coreMQTT-Agent/source/mqtt_agent.c -c -o $@

mqtt_agent_command_functions.o: core_mqtt_config.h
	$(CC) $(FLAGS) $(INCLUDE_DIRS) coreMQTT-Agent/source/mqtt_agent_command_functions.c -c -o $@

mqtt_agent_helper.o: agent_helper/mqtt_agent_helper.c agent_helper/mqtt_agent_helper.h
	$(CC) -undefined dynamic_lookup $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

mqtt_demo_plaintext.o: mqtt_demo/mqtt_demo_plaintext.c
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

mqtt_demo_mutual_auth.o: mqtt_demo/mqtt_demo_mutual_auth.c
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $< -c -o $@

defender_demo.o: $(DEFENDER_OBJECTS)
	$(MAKE) -C defender_demo/
	$(LD) -r $^ -o $@

shadow_demo.o: $(SHADOW_OBJECTS)
	$(LD) -r $^ -o $@

$(SHADOW_OBJECTS): $(SHADOW_SRCS)
	$(MAKE) -C shadow_demo/

clean:
	$(MAKE) -C shadow_demo clean
	rm -rf mqtt_agent_demo mqtt_agent_demo.dSYM *.o

clean_objects:
	$(MAKE) -C shadow_demo clean
	rm -rf *.o *.dSYM
