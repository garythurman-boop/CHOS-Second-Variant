# Freestanding CHOS-native allocation/string runtime, not a host libc link.
LIBC_INCLUDES := -Iuserland/libc/include
LIBC_SOURCES := userland/libc/allocator.c userland/libc/stdlib.c userland/libc/string.c userland/libc/time.c userland/libc/tls.c userland/libc/pthread_sync.c userland/libc/pthread.c userland/libc/emutls_adapter.c userland/libc/cxx_thread_atexit.c userland/libc/numeric.c userland/libc/unicode.c userland/libc/filename.c
LIBC_HEADERS := $(wildcard userland/libc/include/*.h userland/libc/include/chos/*.h)
LIBC_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SOURCES)) $(BUILD)/userland/libc/compiler_rt_emutls.o $(BUILD)/userland/libc/fp_environment.o
$(BUILD)/userland/libc/fp_environment.o: userland/libc/fp_environment.S
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -c $< -o $@
EMUTLS_HEADERS := $(wildcard third_party/compiler-rt/lib/builtins/*.h)
EMUTLS_HOOKS := -Dpthread_key_create=__chos_pthread_key_create_late -Dpthread_mutex_lock=__chos_emutls_mutex_lock -Dpthread_mutex_unlock=__chos_emutls_mutex_unlock -Dpthread_once=__chos_emutls_once -Dpthread_setspecific=__chos_emutls_setspecific
$(BUILD)/userland/libc/compiler_rt_emutls.o: third_party/compiler-rt/lib/builtins/emutls.c $(EMUTLS_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -DEMUTLS_USE_POSIX_MEMALIGN=1 $(EMUTLS_HOOKS) -c $< -o $@
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
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 -fno-builtin $(LIBC_INCLUDES) tests/test_user_allocator.c userland/libc/allocator.c -o $@
$(SAN_DIR)/test-user-allocator: tests/test_user_allocator.c userland/libc/allocator.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation -fno-builtin $(LIBC_INCLUDES) tests/test_user_allocator.c userland/libc/allocator.c -o $@
$(BUILD)/test-libc-string: tests/test_libc_string.c userland/libc/string.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin -DCHOS_LIBC_NO_EXPORTS $(LIBC_INCLUDES) tests/test_libc_string.c userland/libc/string.c -o $@
$(SAN_DIR)/test-libc-string: tests/test_libc_string.c userland/libc/string.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -fno-builtin -DCHOS_LIBC_NO_EXPORTS $(LIBC_INCLUDES) tests/test_libc_string.c userland/libc/string.c -o $@
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
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude $(LIBC_INCLUDES) tests/test_libc_time.c userland/libc/time.c -o $@
$(SAN_DIR)/test-libc-time: tests/test_libc_time.c userland/libc/time.c $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(LIBC_INCLUDES) tests/test_libc_time.c userland/libc/time.c -o $@
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
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 $(PTHREAD_HOST_FLAGS) tests/test_pthread_sync.c userland/libc/pthread_sync.c -o $@
$(SAN_DIR)/test-pthread-sync: tests/test_pthread_sync.c tests/pthread_host_names.h userland/libc/pthread_sync.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(PTHREAD_HOST_FLAGS) tests/test_pthread_sync.c userland/libc/pthread_sync.c -o $@
test-pthread-sync: $(BUILD)/test-pthread-sync
	./$(BUILD)/test-pthread-sync
test-pthread-sync-sanitize: $(SAN_DIR)/test-pthread-sync
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pthread-sync
test: test-pthread-sync
test-sanitize: test-pthread-sync-sanitize
.PHONY: test-pthread-sync test-pthread-sync-sanitize

$(BUILD)/userland/emutls_test.o: userland/examples/emutls_test.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -femulated-tls -c $< -o $@
$(BUILD)/userland/emutls_cpp_test.o: userland/examples/emutls_test.cpp $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c11,$(USER_CFLAGS)) -std=c++17 -fno-exceptions -fno-rtti $(LIBC_INCLUDES) -femulated-tls -c $< -o $@
$(BUILD)/userland/emutls_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/emutls_test.o $(BUILD)/userland/emutls_cpp_test.o $(BUILD)/userland/native_abi.o $(LIBC_OBJECTS) userland/native.ld
	$(LD) -T userland/native.ld -nostdlib -static -z max-page-size=4096 -o $@ $(filter %.o,$^)
test: $(BUILD)/userland/emutls_test.elf

EMUTLS_HOST_FLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -DEMUTLS_USE_POSIX_MEMALIGN=1 -D__emutls_get_address=chos_test_emutls_get_address
$(BUILD)/test-emutls-runtime: tests/test_emutls_runtime.c third_party/compiler-rt/lib/builtins/emutls.c $(EMUTLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(EMUTLS_HOST_FLAGS) -Wall -Wextra -Werror -O2 -pthread $< third_party/compiler-rt/lib/builtins/emutls.c -o $@
$(SAN_DIR)/test-emutls-runtime: tests/test_emutls_runtime.c third_party/compiler-rt/lib/builtins/emutls.c $(EMUTLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(EMUTLS_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread $< third_party/compiler-rt/lib/builtins/emutls.c -o $@
CXX_ATEXIT_HOST_FLAGS := -include tests/cxx_atexit_host_names.h $(LIBC_INCLUDES)
$(BUILD)/test-cxx-thread-atexit: tests/test_cxx_thread_atexit.c tests/cxx_atexit_host_names.h userland/libc/cxx_thread_atexit.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 $(CXX_ATEXIT_HOST_FLAGS) $< userland/libc/cxx_thread_atexit.c -o $@
$(SAN_DIR)/test-cxx-thread-atexit: tests/test_cxx_thread_atexit.c tests/cxx_atexit_host_names.h userland/libc/cxx_thread_atexit.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(CXX_ATEXIT_HOST_FLAGS) $< userland/libc/cxx_thread_atexit.c -o $@
test-emutls-runtime: $(BUILD)/test-emutls-runtime $(BUILD)/test-cxx-thread-atexit $(BUILD)/userland/emutls_test.elf
	python3 tools/verify_compiler_rt.py
	./$(BUILD)/test-emutls-runtime
	./$(BUILD)/test-cxx-thread-atexit
test-emutls-runtime-sanitize: $(SAN_DIR)/test-emutls-runtime $(SAN_DIR)/test-cxx-thread-atexit
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-emutls-runtime
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-cxx-thread-atexit
test: test-emutls-runtime
test-sanitize: test-emutls-runtime-sanitize
.PHONY: test-emutls-runtime test-emutls-runtime-sanitize

BASEC_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/basec_host_names.h -fno-builtin $(LIBC_INCLUDES)
$(BUILD)/test-libc-base: tests/test_libc_base.c tests/basec_host_names.h userland/libc/numeric.c userland/libc/unicode.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 $(BASEC_HOST_FLAGS) tests/test_libc_base.c userland/libc/numeric.c userland/libc/unicode.c -o $@
$(SAN_DIR)/test-libc-base: tests/test_libc_base.c tests/basec_host_names.h userland/libc/numeric.c userland/libc/unicode.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(BASEC_HOST_FLAGS) tests/test_libc_base.c userland/libc/numeric.c userland/libc/unicode.c -o $@
test-libc-base: $(BUILD)/test-libc-base
	./$(BUILD)/test-libc-base
test-libc-base-sanitize: $(SAN_DIR)/test-libc-base
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-libc-base
test: test-libc-base
test-sanitize: test-libc-base-sanitize
.PHONY: test-libc-base test-libc-base-sanitize
$(BUILD)/userland/basec_test.o: userland/examples/basec_test.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -c $< -o $@
$(BUILD)/userland/basec_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/basec_test.o $(BUILD)/userland/native_abi.o $(LIBC_OBJECTS) userland/native.ld
	$(LD) -T userland/native.ld -nostdlib -static -z max-page-size=4096 -o $@ $(filter %.o,$^)
test: $(BUILD)/userland/basec_test.elf

FILENAME_HOST_FLAGS := -DCHOS_HOST_TEST -DCHOS_FILENAME_HOST_TEST -include tests/filename_host_names.h $(LIBC_INCLUDES)
$(BUILD)/test-libc-filename: tests/test_libc_filename.c userland/libc/filename.c $(LIBC_HEADERS) tests/filename_host_names.h
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 $(FILENAME_HOST_FLAGS) tests/test_libc_filename.c userland/libc/filename.c -o $@
$(SAN_DIR)/test-libc-filename: tests/test_libc_filename.c userland/libc/filename.c $(LIBC_HEADERS) tests/filename_host_names.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(FILENAME_HOST_FLAGS) tests/test_libc_filename.c userland/libc/filename.c -o $@
test-libc-filename: $(BUILD)/test-libc-filename
	./$(BUILD)/test-libc-filename
test-libc-filename-sanitize: $(SAN_DIR)/test-libc-filename
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-libc-filename
test: test-libc-filename
test-sanitize: test-libc-filename-sanitize
.PHONY: test-libc-filename test-libc-filename-sanitize
