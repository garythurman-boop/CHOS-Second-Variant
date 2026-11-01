# Original musl conversions over a private bounded memory view. No public
# vfscanf/fscanf or native FILE ABI is exported by this adapter.
MEMORY_SCAN_SOURCES := third_party/musl/src/stdio/vfscanf.c third_party/musl/src/stdio/sscanf.c third_party/musl/src/internal/intscan.c userland/libc/memoryscan/memory.c
MEMORY_SCAN_HEADERS := $(wildcard userland/libc/memoryscan/include/*.h) $(FLOAT_SCAN_HEADERS) third_party/musl/src/internal/intscan.h
MEMORY_SCAN_INCLUDES := -Iuserland/libc/memoryscan/include -Iuserland/libc/floatscan/include $(LIBC_INCLUDES) -Ithird_party/musl/src/internal
MEMORY_SCAN_FLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(MEMORY_SCAN_INCLUDES) -I$(MUSL_MATH_GENERATED) -frounding-math -ffunction-sections -fdata-sections -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses -Wno-sign-compare
MEMORY_SCAN_OBJECTS := $(addprefix $(BUILD)/userland/memoryscan/,$(addsuffix .o,$(basename $(MEMORY_SCAN_SOURCES))))
$(BUILD)/userland/memoryscan/%.o: %.c $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC) userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC) $(MEMORY_SCAN_FLAGS) -MMD -MP -c $< -o $@
-include $(MEMORY_SCAN_OBJECTS:.o=.d)
$(FLOAT_SCAN_ARCHIVE): $(MEMORY_SCAN_OBJECTS)
MEMORY_SCAN_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/floatscan_host_names.h -include tests/memoryscan_host_names.h $(MEMORY_SCAN_INCLUDES) -fno-builtin -frounding-math -Wno-sign-compare -Wno-parentheses
MEMORY_SCAN_HOST_OBJECTS := $(addprefix $(BUILD)/memoryscan-host/,$(addsuffix .o,$(basename $(MEMORY_SCAN_SOURCES))))
MEMORY_SCAN_SAN_OBJECTS := $(addprefix $(SAN_DIR)/memoryscan-host/,$(addsuffix .o,$(basename $(MEMORY_SCAN_SOURCES))))
$(BUILD)/memoryscan-host/%.o: %.c $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h tests/memoryscan_host_names.h userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(MEMORY_SCAN_HOST_FLAGS) -c $< -o $@
$(SAN_DIR)/memoryscan-host/%.o: %.c $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h tests/memoryscan_host_names.h userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(MEMORY_SCAN_HOST_FLAGS) -c $< -o $@
$(BUILD)/test-memoryscan: tests/test_memoryscan.c $(MEMORY_SCAN_HOST_OBJECTS) $(FLOAT_SCAN_HOST_OBJECTS)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin $< $(MEMORY_SCAN_HOST_OBJECTS) $(FLOAT_SCAN_HOST_OBJECTS) -lm -o $@
$(SAN_DIR)/test-memoryscan: tests/test_memoryscan.c $(MEMORY_SCAN_SAN_OBJECTS) $(FLOAT_SCAN_SAN_OBJECTS)
	$(CC_HOST) $(SAN_FLAGS) -fno-builtin $< $(MEMORY_SCAN_SAN_OBJECTS) $(FLOAT_SCAN_SAN_OBJECTS) -lm -o $@
test-memoryscan: $(BUILD)/test-memoryscan
	./$(BUILD)/test-memoryscan
test-memoryscan-sanitize: $(SAN_DIR)/test-memoryscan
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-memoryscan
test: test-memoryscan
test-sanitize: test-memoryscan-sanitize
.PHONY: test-memoryscan test-memoryscan-sanitize

MEMORY_SCAN_GENERATED := $(BUILD)/userland/memoryscan/generated/vfscanf.c
$(MEMORY_SCAN_GENERATED): third_party/musl/src/stdio/vfscanf.c docs/dependencies/musl-memoryscan.json tools/configure_musl_memoryscan.py
	python3 tools/configure_musl_memoryscan.py --out $@
$(BUILD)/userland/memoryscan/third_party/musl/src/stdio/vfscanf.o: $(MEMORY_SCAN_GENERATED) $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h tests/memoryscan_host_names.h userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC) $(MEMORY_SCAN_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/memoryscan-host/third_party/musl/src/stdio/vfscanf.o: $(MEMORY_SCAN_GENERATED) $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h tests/memoryscan_host_names.h userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(MEMORY_SCAN_HOST_FLAGS) -c $< -o $@
$(SAN_DIR)/memoryscan-host/third_party/musl/src/stdio/vfscanf.o: $(MEMORY_SCAN_GENERATED) $(MEMORY_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h tests/memoryscan_host_names.h userland/libc/memoryscan/Makefile.inc
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(MEMORY_SCAN_HOST_FLAGS) -c $< -o $@
