FORMAT_SOURCES := third_party/musl/src/stdio/vfprintf.c third_party/musl/src/stdio/vsnprintf.c third_party/musl/src/stdio/snprintf.c third_party/musl/src/errno/strerror.c
FORMAT_HEADERS := $(wildcard userland/libc/format/include/*.h) third_party/musl/src/errno/__strerror.h
FORMAT_OBJECTS := $(addprefix $(BUILD)/userland/format/,$(FORMAT_SOURCES:.c=.o))
FORMAT_ARCHIVE := $(BUILD)/userland/format/libchos-format.a
FORMAT_INCLUDES := -Iuserland/libc/format/include $(LIBC_INCLUDES)
FORMAT_FLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(FORMAT_INCLUDES) -I$(MUSL_MATH_GENERATED) -frounding-math -ffunction-sections -fdata-sections -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses -Wno-sign-compare
$(BUILD)/userland/format/%.o: %.c $(FORMAT_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(FORMAT_FLAGS) -MMD -MP -c $< -o $@
-include $(FORMAT_OBJECTS:.o=.d)
$(FORMAT_ARCHIVE): $(FORMAT_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
libformat: $(FORMAT_ARCHIVE)
	python3 tools/verify_musl_math.py
.PHONY: libformat
FORMAT_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/format_host_names.h $(FORMAT_INCLUDES) -fno-builtin -frounding-math -Wno-sign-compare -Wno-parentheses
FORMAT_HOST_OBJECTS := $(addprefix $(BUILD)/format-host/,$(FORMAT_SOURCES:.c=.o))
FORMAT_SAN_OBJECTS := $(addprefix $(SAN_DIR)/format-host/,$(FORMAT_SOURCES:.c=.o))
$(BUILD)/format-host/%.o: %.c $(FORMAT_HEADERS) $(LIBC_HEADERS) tests/format_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(FORMAT_HOST_FLAGS) -c $< -o $@
$(SAN_DIR)/format-host/%.o: %.c $(FORMAT_HEADERS) $(LIBC_HEADERS) tests/format_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(FORMAT_HOST_FLAGS) -c $< -o $@
$(BUILD)/test-format: tests/test_format.c $(FORMAT_HOST_OBJECTS)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -frounding-math -Iuserland/libc/include $< $(FORMAT_HOST_OBJECTS) -lm -o $@
$(SAN_DIR)/test-format: tests/test_format.c $(FORMAT_SAN_OBJECTS)
	$(CC_HOST) $(SAN_FLAGS) -fno-builtin -frounding-math -Iuserland/libc/include $< $(FORMAT_SAN_OBJECTS) -lm -o $@
test-format: $(BUILD)/test-format
	./$(BUILD)/test-format
test-format-sanitize: $(SAN_DIR)/test-format
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-format
test: test-format
test-sanitize: test-format-sanitize
.PHONY: test-format test-format-sanitize
$(BUILD)/format-host/third_party/musl/src/stdio/vfprintf.o $(SAN_DIR)/format-host/third_party/musl/src/stdio/vfprintf.o: FORMAT_HOST_FLAGS += -Wno-implicit-fallthrough -Wno-maybe-uninitialized
FORMAT_PRINTF_SOURCE := $(BUILD)/userland/format/vfprintf.c
$(FORMAT_PRINTF_SOURCE): third_party/musl/src/stdio/vfprintf.c docs/dependencies/musl-format.json tools/configure_musl_format.py
	python3 tools/configure_musl_format.py --out $@
$(BUILD)/userland/format/third_party/musl/src/stdio/vfprintf.o: $(FORMAT_PRINTF_SOURCE) $(FORMAT_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(FORMAT_FLAGS) -MMD -MP -c $(FORMAT_PRINTF_SOURCE) -o $@
$(BUILD)/format-host/third_party/musl/src/stdio/vfprintf.o: $(FORMAT_PRINTF_SOURCE) $(FORMAT_HEADERS) $(LIBC_HEADERS) tests/format_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(FORMAT_HOST_FLAGS) -c $(FORMAT_PRINTF_SOURCE) -o $@
$(SAN_DIR)/format-host/third_party/musl/src/stdio/vfprintf.o: $(FORMAT_PRINTF_SOURCE) $(FORMAT_HEADERS) $(LIBC_HEADERS) tests/format_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(FORMAT_HOST_FLAGS) -c $(FORMAT_PRINTF_SOURCE) -o $@
test-native-format: $(BUILD)/userland/format_test.elf
	python3 tools/verify_musl_math.py
	python3 tests/generate_format_vectors.py --check
test: test-native-format
.PHONY: test-native-format
