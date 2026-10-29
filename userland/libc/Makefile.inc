# Freestanding CHOS-native allocation/string runtime, not a host libc link.
LIBC_INCLUDES := -Iinclude -Iuserland/libc/include
LIBC_SOURCES := userland/libc/pread.c userland/libc/strings.c userland/libc/calendar.c userland/libc/mman.c userland/libc/unistd.c userland/libc/platform.c userland/libc/ctype.c userland/libc/inttypes.c userland/libc/assert.c userland/libc/allocator.c userland/libc/stdlib.c userland/libc/string.c userland/libc/time.c userland/libc/tls.c userland/libc/pthread_sync.c userland/libc/pthread.c userland/libc/emutls_adapter.c userland/libc/cxx_thread_atexit.c userland/libc/numeric.c userland/libc/unicode.c userland/libc/filename.c userland/libc/cxx_guard.c userland/libc/exit_registry.c userland/libc/exit.c
LIBC_HEADERS := $(wildcard userland/libc/include/*.h userland/libc/include/chos/*.h userland/libc/include/sys/*.h) include/chos/user_memory_limits.h include/chos/memory_api.h
CALENDAR_SOURCE_FILES := $(addprefix third_party/musl/src/time/,__tm_to_secs.c __secs_to_tm.c __year_to_secs.c __month_to_secs.c)
CALENDAR_NATIVE_OBJECTS := $(patsubst third_party/musl/src/time/%.c,$(BUILD)/userland/libc/calendar/%.o,$(CALENDAR_SOURCE_FILES))
CALENDAR_INCLUDES := -Iuserland/libc/calendar/include
LIBC_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SOURCES)) $(BUILD)/userland/libc/compiler_rt_emutls.o $(BUILD)/userland/libc/fp_environment.o $(BUILD)/userland/libc/musl_memmem.o $(CALENDAR_NATIVE_OBJECTS)
$(BUILD)/userland/libc/fp_environment.o: userland/libc/fp_environment.S
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -c $< -o $@
EMUTLS_HEADERS := $(wildcard third_party/compiler-rt/lib/builtins/*.h)
EMUTLS_HOOKS := -Dpthread_key_create=__chos_pthread_key_create_late -Dpthread_mutex_lock=__chos_emutls_mutex_lock -Dpthread_mutex_unlock=__chos_emutls_mutex_unlock -Dpthread_once=__chos_emutls_once -Dpthread_setspecific=__chos_emutls_setspecific
$(BUILD)/userland/libc/compiler_rt_emutls.o: third_party/compiler-rt/lib/builtins/emutls.c $(EMUTLS_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -DEMUTLS_USE_POSIX_MEMALIGN=1 $(EMUTLS_HOOKS) -c $< -o $@
$(BUILD)/userland/libc/%.o: userland/libc/%.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h userland/libc/internal/exit_registry.h
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

$(BUILD)/test-libc-time: tests/test_libc_time.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude $(LIBC_INCLUDES) $(CALENDAR_INCLUDES) tests/test_libc_time.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) -o $@
$(SAN_DIR)/test-libc-time: tests/test_libc_time.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(LIBC_INCLUDES) $(CALENDAR_INCLUDES) tests/test_libc_time.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) -o $@
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
$(BUILD)/test-libc-base: tests/test_libc_base.c tests/basec_host_names.h userland/libc/numeric.c userland/libc/unicode.c userland/libc/ctype.c userland/libc/inttypes.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 $(BASEC_HOST_FLAGS) tests/test_libc_base.c userland/libc/numeric.c userland/libc/unicode.c userland/libc/ctype.c userland/libc/inttypes.c -o $@
$(SAN_DIR)/test-libc-base: tests/test_libc_base.c tests/basec_host_names.h userland/libc/numeric.c userland/libc/unicode.c userland/libc/ctype.c userland/libc/inttypes.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST $(SAN_FLAGS) -Wno-misleading-indentation $(BASEC_HOST_FLAGS) tests/test_libc_base.c userland/libc/numeric.c userland/libc/unicode.c userland/libc/ctype.c userland/libc/inttypes.c -o $@
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

FILENAME_HOST_FLAGS := -Iinclude -DCHOS_HOST_TEST -DCHOS_FILENAME_HOST_TEST -include tests/filename_host_names.h $(LIBC_INCLUDES)
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
GUARD_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/guard_host_names.h $(LIBC_INCLUDES)
$(BUILD)/test-cxx-guard: tests/test_cxx_guard.c tests/guard_host_names.h userland/libc/cxx_guard.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 $(GUARD_HOST_FLAGS) tests/test_cxx_guard.c userland/libc/cxx_guard.c -o $@
$(SAN_DIR)/test-cxx-guard: tests/test_cxx_guard.c tests/guard_host_names.h userland/libc/cxx_guard.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(GUARD_HOST_FLAGS) tests/test_cxx_guard.c userland/libc/cxx_guard.c -o $@
test-cxx-guard: $(BUILD)/test-cxx-guard
	./$(BUILD)/test-cxx-guard
test-cxx-guard-sanitize: $(SAN_DIR)/test-cxx-guard
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-cxx-guard
test: test-cxx-guard
test-sanitize: test-cxx-guard-sanitize
.PHONY: test-cxx-guard test-cxx-guard-sanitize

NATIVE_STATIC_START := $(BUILD)/userland/libc/static_startup.o
$(BUILD)/test-exit-registry: tests/test_exit_registry.c userland/libc/exit_registry.c userland/libc/internal/exit_registry.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 -pthread -Iuserland/libc tests/test_exit_registry.c userland/libc/exit_registry.c -o $@
$(SAN_DIR)/test-exit-registry: tests/test_exit_registry.c userland/libc/exit_registry.c userland/libc/internal/exit_registry.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -pthread -Iuserland/libc tests/test_exit_registry.c userland/libc/exit_registry.c -o $@
test-exit-registry: $(BUILD)/test-exit-registry
	./$(BUILD)/test-exit-registry
test-exit-registry-sanitize: $(SAN_DIR)/test-exit-registry
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-exit-registry
test: test-exit-registry
test-sanitize: test-exit-registry-sanitize
.PHONY: test-exit-registry test-exit-registry-sanitize

PLATFORM_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/platform_host_names.h $(LIBC_INCLUDES) -Iinclude -Wno-misleading-indentation -fno-builtin
$(BUILD)/test-libc-platform: tests/test_libc_platform.c tests/platform_host_names.h userland/libc/platform.c userland/libc/time.c $(CALENDAR_SOURCE_FILES) userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(PLATFORM_HOST_FLAGS) $(CALENDAR_INCLUDES) tests/test_libc_platform.c userland/libc/platform.c userland/libc/time.c $(CALENDAR_SOURCE_FILES) -o $@
$(SAN_DIR)/test-libc-platform: tests/test_libc_platform.c tests/platform_host_names.h userland/libc/platform.c userland/libc/time.c $(CALENDAR_SOURCE_FILES) userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) include/chos/clock_api.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(PLATFORM_HOST_FLAGS) $(CALENDAR_INCLUDES) tests/test_libc_platform.c userland/libc/platform.c userland/libc/time.c $(CALENDAR_SOURCE_FILES) -o $@
test-libc-platform: $(BUILD)/test-libc-platform
	./$(BUILD)/test-libc-platform
test-libc-platform-sanitize: $(SAN_DIR)/test-libc-platform
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-libc-platform
test: test-libc-platform
test-sanitize: test-libc-platform-sanitize
.PHONY: test-libc-platform test-libc-platform-sanitize

# Original maintained GNU binary search primitive; no host libc is linked.
$(BUILD)/userland/libc/musl_memmem.o: third_party/musl/src/string/memmem.c $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -Wno-sign-compare -c $< -o $@

$(BUILD)/memmem-host.o: third_party/musl/src/string/memmem.c
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -Wno-sign-compare -fno-builtin -Dmemmem=chos_test_memmem -c $< -o $@
$(SAN_DIR)/memmem-host.o: third_party/musl/src/string/memmem.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Wno-sign-compare -fno-builtin -Dmemmem=chos_test_memmem -c $< -o $@
$(BUILD)/test-memmem: tests/test_memmem.c $(BUILD)/memmem-host.o
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin $^ -o $@
$(SAN_DIR)/test-memmem: tests/test_memmem.c $(SAN_DIR)/memmem-host.o
	$(CC_HOST) $(SAN_FLAGS) -fno-builtin $^ -o $@
test-memmem: $(BUILD)/test-memmem
	./$(BUILD)/test-memmem
test-memmem-sanitize: $(SAN_DIR)/test-memmem
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-memmem
test: test-memmem
test-sanitize: test-memmem-sanitize
.PHONY: test-memmem test-memmem-sanitize

$(BUILD)/userland/libc/calendar/%.o: third_party/musl/src/time/%.c userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) $(CALENDAR_INCLUDES) -c $< -o $@

CALENDAR_HOST_FLAGS := -DCHOS_HOST_TEST -include tests/calendar_host_names.h $(LIBC_INCLUDES) $(CALENDAR_INCLUDES) -Wno-misleading-indentation
$(BUILD)/calendar-host-oracle.o: tests/calendar_host_oracle.c
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -c $< -o $@
$(SAN_DIR)/calendar-host-oracle.o: tests/calendar_host_oracle.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -c $< -o $@
$(BUILD)/test-calendar: tests/test_calendar.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) tests/calendar_host_names.h userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) $(BUILD)/calendar-host-oracle.o $(BUILD)/stdio-host-threading.o
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror -pthread $(CALENDAR_HOST_FLAGS) tests/test_calendar.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) $(BUILD)/calendar-host-oracle.o $(BUILD)/stdio-host-threading.o -o $@
$(SAN_DIR)/test-calendar: tests/test_calendar.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) tests/calendar_host_names.h userland/libc/calendar/include/time_impl.h $(LIBC_HEADERS) $(SAN_DIR)/calendar-host-oracle.o $(SAN_DIR)/stdio-host-threading.o
	$(CC_HOST) $(SAN_FLAGS) -pthread $(CALENDAR_HOST_FLAGS) tests/test_calendar.c userland/libc/time.c userland/libc/calendar.c $(CALENDAR_SOURCE_FILES) $(SAN_DIR)/calendar-host-oracle.o $(SAN_DIR)/stdio-host-threading.o -o $@
test-calendar: $(BUILD)/test-calendar
	./$(BUILD)/test-calendar
test-calendar-sanitize: $(SAN_DIR)/test-calendar
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-calendar
test: test-calendar
test-sanitize: test-calendar-sanitize
.PHONY: test-calendar test-calendar-sanitize

test-calendar-vectors:
	python3 tests/generate_calendar_vectors.py --check
test: test-calendar-vectors
.PHONY: test-calendar-vectors

$(BUILD)/test-pread: tests/test_pread.c userland/libc/pread.c $(LIBC_HEADERS) include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 -DCHOS_PREAD_HOST_TEST -Dpread=chos_test_pread $(LIBC_INCLUDES) tests/test_pread.c userland/libc/pread.c -o $@
$(SAN_DIR)/test-pread: tests/test_pread.c userland/libc/pread.c $(LIBC_HEADERS) include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DCHOS_PREAD_HOST_TEST -Dpread=chos_test_pread $(LIBC_INCLUDES) tests/test_pread.c userland/libc/pread.c -o $@
test-pread: $(BUILD)/test-pread
	./$(BUILD)/test-pread
test-pread-sanitize: $(SAN_DIR)/test-pread
	./$(SAN_DIR)/test-pread
test: test-pread
test-sanitize: test-pread-sanitize
.PHONY: test-pread test-pread-sanitize
