CC = gcc
CFLAGS = -Wall -Wextra -g
BUILD_DIR = build
ECHO_TARGETS = echo_server echo_client
MODBUS_TARGETS = check_modbus modbus_server modbus_client modbus_gateway
THREAD_TARGETS = gateway_worker_demo

MODBUS_PKGCONFIG_DIR ?= $(HOME)/.local/libmodbus-3.2.0/lib/pkgconfig
MODBUS_PKG_CONFIG = PKG_CONFIG_PATH="$(MODBUS_PKGCONFIG_DIR)$(if $(PKG_CONFIG_PATH),:$(PKG_CONFIG_PATH))" pkg-config

.PHONY: all clean check-modbus $(ECHO_TARGETS) $(MODBUS_TARGETS) $(THREAD_TARGETS)

all: $(addprefix $(BUILD_DIR)/,$(ECHO_TARGETS))

$(ECHO_TARGETS) $(MODBUS_TARGETS) $(THREAD_TARGETS): %: $(BUILD_DIR)/%

$(BUILD_DIR):
	mkdir -p $@

$(addprefix $(BUILD_DIR)/,$(ECHO_TARGETS)): $(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

check-modbus: $(BUILD_DIR)/check_modbus
	./$(BUILD_DIR)/check_modbus

$(addprefix $(BUILD_DIR)/,$(MODBUS_TARGETS)): $(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	@modbus_flags=$$($(MODBUS_PKG_CONFIG) --print-errors --cflags --libs 'libmodbus >= 3.2.0') && \
		$(CC) $(CFLAGS) $< -o $@ $$modbus_flags

$(addprefix $(BUILD_DIR)/,$(THREAD_TARGETS)): $(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -pthread $< -o $@

clean:
	rm -f $(addprefix $(BUILD_DIR)/,$(ECHO_TARGETS) $(MODBUS_TARGETS) $(THREAD_TARGETS))
