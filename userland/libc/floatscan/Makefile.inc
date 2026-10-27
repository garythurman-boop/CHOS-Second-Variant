# Real floating parsing in a separately extracted archive: legacy object-only
# application links do not acquire a mandatory binary128/math dependency.
FLOAT_SCAN_HEADERS := $(wildcard userland/libc/floatscan/include/*.h) third_party/musl/src/internal/shgetc.h third_party/musl/src/internal/floatscan.h
FLOAT_SCAN_SOURCES := third_party/musl/src/stdlib/strtod.c third_party/musl/src/stdlib/atof.c third_party/musl/src/internal/floatscan.c userland/libc/floatscan/stream.c
FLOAT_SCAN_OBJECTS := $(addprefix $(BUILD)/userland/floatscan/,$(addsuffix .o,$(basename $(FLOAT_SCAN_SOURCES))))
FLOAT_SCAN_INCLUDES := -Iuserland/libc/floatscan/include $(LIBC_INCLUDES) -Ithird_party/musl/src/internal
FLOAT_SCAN_FLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(FLOAT_SCAN_INCLUDES) -I$(MUSL_MATH_GENERATED) -frounding-math -ffunction-sections -fdata-sections -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses -Wno-sign-compare
FLOAT_SCAN_ARCHIVE := $(BUILD)/userland/floatscan/libchos-floatparse.a
$(BUILD)/userland/floatscan/%.o: %.c $(FLOAT_SCAN_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(FLOAT_SCAN_FLAGS) -MMD -MP -c $< -o $@
-include $(FLOAT_SCAN_OBJECTS:.o=.d)
$(FLOAT_SCAN_ARCHIVE): $(FLOAT_SCAN_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
floatparse: $(FLOAT_SCAN_ARCHIVE)
.PHONY: floatparse
FLOAT_SCAN_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/floatscan_host_names.h $(FLOAT_SCAN_INCLUDES) -fno-builtin -frounding-math -Wno-sign-compare -Wno-parentheses
FLOAT_SCAN_HOST_OBJECTS := $(addprefix $(BUILD)/floatscan-host/,$(addsuffix .o,$(basename $(FLOAT_SCAN_SOURCES))))
FLOAT_SCAN_SAN_OBJECTS := $(addprefix $(SAN_DIR)/floatscan-host/,$(addsuffix .o,$(basename $(FLOAT_SCAN_SOURCES))))
$(BUILD)/floatscan-host/%.o: %.c $(FLOAT_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(FLOAT_SCAN_HOST_FLAGS) -c $< -o $@
$(SAN_DIR)/floatscan-host/%.o: %.c $(FLOAT_SCAN_HEADERS) $(LIBC_HEADERS) tests/floatscan_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(FLOAT_SCAN_HOST_FLAGS) -c $< -o $@
$(BUILD)/test-floatscan: tests/test_floatscan.c $(FLOAT_SCAN_HOST_OBJECTS) $(LIBC_HEADERS) tests/floatscan_host_names.h
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -DCHOS_HOST_TEST -include tests/floatscan_host_names.h $(LIBC_INCLUDES) -fno-builtin $< $(FLOAT_SCAN_HOST_OBJECTS) -lm -o $@
$(SAN_DIR)/test-floatscan: tests/test_floatscan.c $(FLOAT_SCAN_SAN_OBJECTS) $(LIBC_HEADERS) tests/floatscan_host_names.h
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST -include tests/floatscan_host_names.h $(LIBC_INCLUDES) -fno-builtin $< $(FLOAT_SCAN_SAN_OBJECTS) -lm -o $@
test-floatscan: $(BUILD)/test-floatscan
	./$(BUILD)/test-floatscan
test-floatscan-sanitize: $(SAN_DIR)/test-floatscan
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-floatscan
test: test-floatscan
test-sanitize: test-floatscan-sanitize
.PHONY: test-floatscan test-floatscan-sanitize
$(BUILD)/userland/floatparse_test.o: userland/examples/floatparse_test.c tests/floatparse_vectors.h tests/quad_vectors.h $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(LIBC_INCLUDES) -I$(MUSL_MATH_GENERATED) -Itests -frounding-math -MMD -MP -c $< -o $@
-include $(BUILD)/userland/floatparse_test.d
$(BUILD)/userland/floatparse_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/floatparse_test.o $(FLOAT_SCAN_ARCHIVE) $(QUAD_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
test-native-floatparse: $(BUILD)/userland/floatparse_test.elf
	python3 tools/verify_musl_math.py
	python3 tools/generate_floatparse_vectors.py --check
test: test-native-floatparse
.PHONY: test-native-floatparse
