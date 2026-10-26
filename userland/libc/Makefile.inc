# Freestanding CHOS-native allocation/string runtime, not a host libc link.
LIBC_INCLUDES := -Iuserland/libc/include
LIBC_SOURCES := userland/libc/allocator.c userland/libc/stdlib.c userland/libc/string.c userland/libc/time.c userland/libc/tls.c userland/libc/pthread_sync.c userland/libc/pthread.c
LIBC_HEADERS := $(wildcard userland/libc/include/*.h userland/libc/include/chos/*.h)
LIBC_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SOURCES))
$(BUILD)/userland/libc/%.o: userland/libc/%.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -c $< -o $@
$(BUILD)/userland/runtime_test.o: userland/examples/runtime_test.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -c $< -o $@
$(BUILD)/userland/runtime_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/runtime_test.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/libc-cpp-api.o $(LIBC_OBJECTS) userland/native.ld
	$(LD) -T userland/native.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/runtime_test.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/libc-cpp-api.o $(LIBC_OBJECTS)
$(BUILD)/test-user-allocator: tests/test_user_allocator.c userland/libc/allocator.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 -fno-builtin $(LIBC_INCLUDES) tests/test_user_allocator.c userland/libc/allocator.c -o $@
$(SAN_DIR)/test-user-allocator: tests/test_user_allocator.c userland/libc/allocator.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Wno-misleading-indentation -fno-builtin $(LIBC_INCLUDES) tests/test_user_allocator.c userland/libc/allocator.c -o $@
$(BUILD)/test-libc-string: tests/test_libc_string.c userland/libc/string.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin -DCHOS_LIBC_NO_EXPORTS $(LIBC_INCLUDES) tests/test_libc_string.c userland/libc/string.c -o $@
$(SAN_DIR)/test-libc-string: tests/test_libc_string.c userland/libc/string.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -fno-builtin -DCHOS_LIBC_NO_EXPORTS $(LIBC_INCLUDES) tests/test_libc_string.c userland/libc/string.c -o $@
test-user-allocator: $(BUILD)/test-user-allocator $(BUILD)/test-libc-string
	./$(BUILD)/test-user-allocator
	./$(BUILD)/test-libc-string
test-user-allocator-sanitize: $(SAN_DIR)/test-user-allocator $(SAN_DIR)/test-libc-string
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-user-allocator
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-libc-string
$(BUILD)/userland/libc-cpp-api.o: tests/test_libc_cpp_api.cpp $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c11,$(USER_CFLAGS)) -std=c++17 -fno-exceptions -fno-rtti $(LIBC_INCLUDES) -c $< -o $@
test test-userland: $(BUILD)/userland/runtime_test.elf $(BUILD)/userland/libc-cpp-api.o
test: test-user-allocator
test-sanitize: test-user-allocator-sanitize
.PHONY: test-user-allocator test-user-allocator-sanitize

$(BUILD)/test-libc-time: tests/test_libc_time.c userland/libc/time.c $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -DCHOS_HOST_TEST -Iinclude $(LIBC_INCLUDES) tests/test_libc_time.c userland/libc/time.c -o $@
$(SAN_DIR)/test-libc-time: tests/test_libc_time.c userland/libc/time.c $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Wno-misleading-indentation -DCHOS_HOST_TEST $(LIBC_INCLUDES) tests/test_libc_time.c userland/libc/time.c -o $@
test-libc-time: $(BUILD)/test-libc-time
	./$(BUILD)/test-libc-time
test-libc-time-sanitize: $(SAN_DIR)/test-libc-time
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-libc-time
test: test-libc-time
test-sanitize: test-libc-time-sanitize
.PHONY: test-libc-time test-libc-time-sanitize
$(BUILD)/userland/pthread_test.o: userland/examples/pthread_test.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -c $< -o $@
$(BUILD)/userland/pthread_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/pthread_test.o $(BUILD)/userland/native_abi.o $(LIBC_OBJECTS) userland/native.ld
	$(LD) -T userland/native.ld -nostdlib -static -z max-page-size=4096 -o $@ $(filter %.o,$^)
test: $(BUILD)/userland/pthread_test.elf

PTHREAD_HOST_FLAGS := -DCHOS_PTHREAD_HOST_TEST -include tests/pthread_host_names.h $(LIBC_INCLUDES)
$(BUILD)/test-pthread-sync: tests/test_pthread_sync.c tests/pthread_host_names.h userland/libc/pthread_sync.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 $(PTHREAD_HOST_FLAGS) tests/test_pthread_sync.c userland/libc/pthread_sync.c -o $@
$(SAN_DIR)/test-pthread-sync: tests/test_pthread_sync.c tests/pthread_host_names.h userland/libc/pthread_sync.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Wno-misleading-indentation $(PTHREAD_HOST_FLAGS) tests/test_pthread_sync.c userland/libc/pthread_sync.c -o $@
test-pthread-sync: $(BUILD)/test-pthread-sync
	./$(BUILD)/test-pthread-sync
test-pthread-sync-sanitize: $(SAN_DIR)/test-pthread-sync
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pthread-sync
test: test-pthread-sync
test-sanitize: test-pthread-sync-sanitize
.PHONY: test-pthread-sync test-pthread-sync-sanitize
