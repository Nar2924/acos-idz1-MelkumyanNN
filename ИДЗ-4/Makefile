CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L

BUILD = .build
PROGRAMS = garden_app gardener_app monitor_app
SERVER_OBJECTS = $(addprefix $(BUILD)/,garden/main.o garden/garden.o garden/server.o common/config.o common/net.o)
GARDENER_OBJECTS = $(addprefix $(BUILD)/,gardener/main.o common/client.o common/net.o)
MONITOR_OBJECTS = $(addprefix $(BUILD)/,monitor/main.o common/client.o common/net.o)
OBJECTS = $(sort $(SERVER_OBJECTS) $(GARDENER_OBJECTS) $(MONITOR_OBJECTS))

.PHONY: all test clean
all: $(PROGRAMS)

garden_app: $(SERVER_OBJECTS)
gardener_app: $(GARDENER_OBJECTS)
monitor_app: $(MONITOR_OBJECTS)

$(PROGRAMS):
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

test: all
	python3 tests/integration.py

clean:
	rm -rf $(BUILD) $(PROGRAMS)

-include $(OBJECTS:.o=.d)

LOCAL_BUILD = $(BUILD)/inherited
LOCAL_NET = $(LOCAL_BUILD)/net.o $(BUILD)/tests/inherited_sockets.o

.PHONY: test-local
test-local: $(addprefix $(LOCAL_BUILD)/,$(PROGRAMS))
	python3 tests/integration.py --inherited $(LOCAL_BUILD)

$(LOCAL_BUILD)/garden_app: $(filter-out $(BUILD)/common/net.o $(BUILD)/garden/server.o,$(SERVER_OBJECTS)) $(LOCAL_BUILD)/server.o $(LOCAL_NET)
$(LOCAL_BUILD)/gardener_app: $(filter-out $(BUILD)/common/net.o,$(GARDENER_OBJECTS)) $(LOCAL_NET)
$(LOCAL_BUILD)/monitor_app: $(filter-out $(BUILD)/common/net.o,$(MONITOR_OBJECTS)) $(LOCAL_NET)

$(addprefix $(LOCAL_BUILD)/,$(PROGRAMS)):
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(LOCAL_BUILD)/net.o: common/net.c common/net.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dtcp_open_listener=native_tcp_open_listener \
		-Dtcp_connect=native_tcp_connect -c $< -o $@

$(LOCAL_BUILD)/server.o: garden/server.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -Daccept=inherited_accept -c $< -o $@

-include $(LOCAL_BUILD)/server.d $(BUILD)/tests/inherited_sockets.d
