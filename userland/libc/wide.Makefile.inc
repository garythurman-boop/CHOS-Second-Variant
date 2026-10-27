# Wide memory/string and fixed UTF8 conversion, extracted only by users.
WIDE_SOURCES := $(shell python3 -c 'import json;print(" ".join("third_party/musl/"+p for p in json.load(open("docs/dependencies/musl-wide.json"))["sources"]))') userland/libc/wide.c userland/libc/wide_ctype.c
WIDE_OBJECTS := $(addprefix $(BUILD)/userland/libwide/,$(addsuffix .o,$(basename $(WIDE_SOURCES))))
WIDE_ARCHIVE := $(BUILD)/userland/libwide/libchos-wide.a
$(BUILD)/userland/libwide/%.o: %.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -ffunction-sections -fdata-sections -Wno-misleading-indentation -MMD -MP -c $< -o $@
-include $(WIDE_OBJECTS:.o=.d)
$(WIDE_ARCHIVE): $(WIDE_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
libwide: $(WIDE_ARCHIVE)
.PHONY: libwide
$(BUILD)/userland/libwide/third_party/musl/src/string/wcsstr.o: USER_CFLAGS += -Wno-sign-compare
WIDE_HOST_SOURCES := $(WIDE_SOURCES) userland/libc/unicode.c userland/libc/ctype.c
WIDE_HOST_OBJECTS := $(addprefix $(BUILD)/wide-host/,$(addsuffix .o,$(basename $(WIDE_HOST_SOURCES))))
WIDE_SAN_OBJECTS := $(addprefix $(SAN_DIR)/wide-host/,$(addsuffix .o,$(basename $(WIDE_HOST_SOURCES))))
WIDE_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/wide_host_names.h $(LIBC_INCLUDES) -fno-builtin -Wno-misleading-indentation
$(BUILD)/wide-host/%.o: %.c $(LIBC_HEADERS) tests/wide_host_names.h tests/basec_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(WIDE_HOST_FLAGS) $(WIDE_HOST_WARNINGS) -c $< -o $@
$(SAN_DIR)/wide-host/%.o: %.c $(LIBC_HEADERS) tests/wide_host_names.h tests/basec_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(WIDE_HOST_FLAGS) $(WIDE_HOST_WARNINGS) -c $< -o $@
$(BUILD)/wide-host/third_party/musl/src/string/wcsstr.o $(SAN_DIR)/wide-host/third_party/musl/src/string/wcsstr.o: WIDE_HOST_WARNINGS += -Wno-sign-compare
$(BUILD)/test-wide: tests/test_wide.c $(WIDE_HOST_OBJECTS) $(LIBC_HEADERS) tests/wide_host_names.h
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(WIDE_HOST_FLAGS) $< $(WIDE_HOST_OBJECTS) -o $@
$(SAN_DIR)/test-wide: tests/test_wide.c $(WIDE_SAN_OBJECTS) $(LIBC_HEADERS) tests/wide_host_names.h
	$(CC_HOST) $(SAN_FLAGS) $(WIDE_HOST_FLAGS) $< $(WIDE_SAN_OBJECTS) -o $@
test-wide: $(BUILD)/test-wide
	./$(BUILD)/test-wide
test-wide-sanitize: $(SAN_DIR)/test-wide
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-wide
test: test-wide
test-sanitize: test-wide-sanitize
.PHONY: test-wide test-wide-sanitize
$(BUILD)/wide-host/third_party/musl/src/string/wcscspn.o $(SAN_DIR)/wide-host/third_party/musl/src/string/wcscspn.o: WIDE_HOST_WARNINGS += -Wno-sign-compare
