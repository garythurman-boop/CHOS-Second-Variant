# Read-only native streams, only extracted when dependencies request them.
STDIO_ARCHIVE := $(BUILD)/userland/libstdio/libchos-stdio.a
$(BUILD)/userland/libstdio/stdio_read.o: userland/libc/stdio_read.c $(LIBC_HEADERS) include/chos/file_position.h userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -ffunction-sections -fdata-sections -MMD -MP -c $< -o $@
-include $(BUILD)/userland/libstdio/stdio_read.d
$(STDIO_ARCHIVE): $(BUILD)/userland/libstdio/stdio_read.o
	rm -f $@
	llvm-ar rcs $@ $^
libstdio: $(STDIO_ARCHIVE)
.PHONY: libstdio
STDIO_HOST_FLAGS := -DCHOS_HOST_TEST -DCHOS_STDIO_HOST_TEST -include tests/stdio_host_names.h $(LIBC_INCLUDES) -Iinclude -Wno-misleading-indentation -fno-builtin
$(BUILD)/test-stdio-read: tests/test_stdio_read.c userland/libc/stdio_read.c tests/stdio_host_names.h $(LIBC_HEADERS) include/chos/file_position.h include/chos/file_api.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -pthread $(STDIO_HOST_FLAGS) tests/test_stdio_read.c userland/libc/stdio_read.c $(STDIO_THREAD_OBJECT) -o $@
$(SAN_DIR)/test-stdio-read: tests/test_stdio_read.c userland/libc/stdio_read.c tests/stdio_host_names.h $(LIBC_HEADERS) include/chos/file_position.h include/chos/file_api.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -pthread $(STDIO_HOST_FLAGS) tests/test_stdio_read.c userland/libc/stdio_read.c $(STDIO_THREAD_OBJECT) -o $@
test-stdio-read: $(BUILD)/test-stdio-read
	./$(BUILD)/test-stdio-read
test-stdio-read-sanitize: $(SAN_DIR)/test-stdio-read
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-stdio-read
test: test-stdio-read
test-sanitize: test-stdio-read-sanitize
.PHONY: test-stdio-read test-stdio-read-sanitize

$(BUILD)/stdio-host-threading.o: tests/stdio_host_threading.c
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -pthread -c $< -o $@
$(SAN_DIR)/stdio-host-threading.o: tests/stdio_host_threading.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -pthread -c $< -o $@
$(BUILD)/test-stdio-read: STDIO_THREAD_OBJECT := $(BUILD)/stdio-host-threading.o
$(SAN_DIR)/test-stdio-read: STDIO_THREAD_OBJECT := $(SAN_DIR)/stdio-host-threading.o
$(BUILD)/test-stdio-read: $(BUILD)/stdio-host-threading.o
$(SAN_DIR)/test-stdio-read: $(SAN_DIR)/stdio-host-threading.o
