CC ?= gcc

INCLUDE_DIRS = -I . \
               -I ../agent_helper/ \
               -I ../ \
               -I ../../../libraries/standard/coreMQTT/source/include \
               -I ../../../libraries/standard/coreMQTT/source/interface \
               -I ../../../libraries/standard/coreJSON/source/include \
               -I ../../../libraries/aws/device-shadow-for-aws-iot-embedded-sdk/source/include \
               -I ../../../libraries/standard/backoffAlgorithm/source/include \
               -I ../../../platform/include \
               -I ../../../platform/posix/transport/include \
               -I ../../logging-stack/ \
	       -I ./mqtt_demo/

SRC_FILES = $(shell find ./ -name '*.c') \
            $(shell find ../../../libraries/aws/device-shadow-for-aws-iot-embedded-sdk/source -name '*.c') \
            $(shell find ../../../libraries/standard/coreJSON/source -name '*.c')

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

LDFLAGS = -L/usr/local/opt/openssl@1.1/lib -lssl -lcrypto

#all: mqtt_agent.o mqtt_agent_helper.o mqtt_demo_plaintext.o mqtt_demo_mutual_auth.o
#	$(CC) $(FLAGS) $(LDFLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -o mqtt_agent_demo
#	$(CC) $(FLAGS) $(LDFLAGS) $(INCLUDE_DIRS) $(SRC_FILES) $^ -o mqtt_agent_demo

shadow_demo.o:
#	$(CC) $(FLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -c -o $@
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -c 

clean:
	rm -rf *.o
