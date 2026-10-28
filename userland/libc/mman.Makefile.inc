MMAN_HOST_FLAGS := -DCHOS_HOST_TEST -DCHOS_MMAN_HOST_TEST -DCHOS_UNISTD_HOST_TEST -include tests/mman_host_names.h $(LIBC_INCLUDES)
MMAN_TEST_SOURCES := tests/test_libc_mman.c userland/libc/mman.c userland/libc/unistd.c
$(BUILD)/test-libc-mman: $(MMAN_TEST_SOURCES) tests/mman_host_names.h $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -O2 -Wall -Wextra -Werror $(MMAN_HOST_FLAGS) $(MMAN_TEST_SOURCES) -o $@
$(SAN_DIR)/test-libc-mman: $(MMAN_TEST_SOURCES) tests/mman_host_names.h $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(MMAN_HOST_FLAGS) $(MMAN_TEST_SOURCES) -o $@
test-libc-mman: $(BUILD)/test-libc-mman
	./$(BUILD)/test-libc-mman
test-libc-mman-sanitize: $(SAN_DIR)/test-libc-mman
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-libc-mman
test: test-libc-mman
test-sanitize: test-libc-mman-sanitize
.PHONY: test-libc-mman test-libc-mman-sanitize

$(BUILD)/userland/vm_test.o: userland/examples/vm_test.c $(LIBC_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(LIBC_INCLUDES) -c $< -o $@
$(BUILD)/userland/vm_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/vm_test.o $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^)
vm-test: $(BUILD)/userland/vm_test.elf
.PHONY: vm-test
test: vm-test
