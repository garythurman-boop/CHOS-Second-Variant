$(BUILD)/userland/expected_hash_test.o: userland/examples/expected_hash_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -c $< -o $@
$(BUILD)/userland/expected_hash_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/expected_hash_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
expected-hash-test: $(BUILD)/userland/expected_hash_test.elf
.PHONY: expected-hash-test
test: expected-hash-test
