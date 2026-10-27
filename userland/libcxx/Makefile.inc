# Pinned real libc++ allocation/algorithm sources and bounded container profile.
LIBCPP_ROOT := third_party/libcxx
LIBCPP_GENERATED := $(BUILD)/userland/libcxx/include
LIBCPP_CONFIG := $(LIBCPP_GENERATED)/__config_site $(LIBCPP_GENERATED)/__assertion_handler
LIBCPP_HEADERS := $(shell rg --files $(LIBCPP_ROOT) userland/libcxx/include)
# C++ wrappers must precede C headers, including for their include_next contracts.
LIBCPP_INCLUDES := -nostdinc++ -isystem $(LIBCPP_GENERATED) -isystem userland/libcxx/include -isystem $(LIBCPP_ROOT)/include -isystem userland/libc/include -isystem $(MUSL_MATH_GENERATED)
LIBCPP_FLAGS := $(filter-out -std=c11 -mgeneral-regs-only,$(USER_CFLAGS)) -std=c++20 -fno-exceptions -fno-rtti -femulated-tls -ffunction-sections -fdata-sections $(LIBCPP_INCLUDES)
LIBCPP_SOURCES := $(LIBCPP_ROOT)/src/new.cpp $(LIBCPP_ROOT)/src/new_handler.cpp $(LIBCPP_ROOT)/src/new_helpers.cpp $(LIBCPP_ROOT)/src/algorithm.cpp userland/libcxx/string_instantiations.cpp userland/libcxx/verbose_abort.cpp
LIBCPP_OBJECTS := $(addprefix $(BUILD)/userland/libcxx/,$(addsuffix .o,$(basename $(LIBCPP_SOURCES))))
LIBCPP_ARCHIVE := $(BUILD)/userland/libcxx/libchos-c++.a
$(LIBCPP_GENERATED)/__config_site: tools/configure_libcxx.py $(LIBCPP_ROOT)/include/__config_site.in $(LIBCPP_ROOT)/vendor/llvm/default_assertion_handler.in
	python3 tools/configure_libcxx.py --out $(LIBCPP_GENERATED)
$(LIBCPP_GENERATED)/__assertion_handler: $(LIBCPP_GENERATED)/__config_site $(LIBCPP_ROOT)/vendor/llvm/default_assertion_handler.in
	@mkdir -p $(dir $@)
	cp $(LIBCPP_ROOT)/vendor/llvm/default_assertion_handler.in $@
$(BUILD)/userland/libcxx/%.o: %.cpp $(LIBCPP_HEADERS) $(LIBCPP_CONFIG) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/userland/libcxx/third_party/libcxx/src/algorithm.o: LIBCPP_FLAGS += -Wno-unused-parameter
-include $(LIBCPP_OBJECTS:.o=.d)
$(LIBCPP_ARCHIVE): $(LIBCPP_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
libcxx: $(LIBCPP_ARCHIVE)
$(BUILD)/userland/cxx_test.o: userland/examples/cxx_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_test.d
$(BUILD)/userland/cxx_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_test.elf
	python3 tools/verify_libcxx.py
test: test-libcxx
.PHONY: libcxx test-libcxx
$(BUILD)/userland/cxx_guard_test.o: userland/examples/cxx_guard_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_guard_test.d
$(BUILD)/userland/cxx_guard_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_guard_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_guard_test.elf

$(BUILD)/userland/cxx23_test.o: userland/examples/cxx_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx23_test.d
$(BUILD)/userland/cxx23_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx23_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx23_test.elf
$(BUILD)/userland/static_startup_test.o: userland/examples/static_startup_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/userland/static_startup_early_test.o: userland/examples/static_startup_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -DSTARTUP_EARLY_EXIT -MMD -MP -c $< -o $@
-include $(BUILD)/userland/static_startup_test.d $(BUILD)/userland/static_startup_early_test.d
$(BUILD)/userland/static_startup_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/static_startup_test.o $(BUILD)/userland/static_startup_postkey.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
$(BUILD)/userland/static_startup_early_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/static_startup_early_test.o $(BUILD)/userland/static_startup_postkey.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/static_startup_test.elf $(BUILD)/userland/static_startup_early_test.elf
test-static-startup-link: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o
	python3 tests/test_static_startup_link.py --cc $(CC) --ld $(LD)
test: test-static-startup-link
.PHONY: test-static-startup-link

$(BUILD)/userland/static_startup_postkey.o: userland/examples/static_startup_postkey.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/static_startup_postkey.d
