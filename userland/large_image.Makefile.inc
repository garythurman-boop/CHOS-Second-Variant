$(BUILD)/userland/large_image_data.o: userland/examples/large_image_data.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/userland/large_image_test.o: userland/examples/large_image_test.c include/chos/user_memory_limits.h userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/userland/large_image_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/large_image_test.o $(BUILD)/userland/large_image_data.o userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^)
large-image-test: $(BUILD)/userland/large_image_test.elf
.PHONY: large-image-test
test: large-image-test
