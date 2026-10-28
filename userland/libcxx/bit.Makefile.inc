$(BUILD)/userland/bit_test.o: userland/examples/bit_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -Werror -c $< -o $@
$(BUILD)/userland/bit_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/bit_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
bit-test: $(BUILD)/userland/bit_test.elf
	python3 tools/verify_libcxx.py
.PHONY: bit-test
test: bit-test
