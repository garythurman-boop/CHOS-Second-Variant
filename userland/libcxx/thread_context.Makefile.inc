$(BUILD)/userland/thread_context_probe.o: userland/examples/thread_context_probe.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/userland/thread_context_test.o: userland/examples/thread_context_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) include/chos/thread_context_api.h userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/thread_context_test.d
$(BUILD)/userland/thread_context_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/thread_context_test.o $(BUILD)/userland/thread_context_probe.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
thread-context-test: $(BUILD)/userland/thread_context_test.elf
.PHONY: thread-context-test
test: thread-context-test
