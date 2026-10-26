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
$(LIBCPP_CONFIG) &: tools/configure_libcxx.py $(LIBCPP_ROOT)/include/__config_site.in $(LIBCPP_ROOT)/vendor/llvm/default_assertion_handler.in
	python3 tools/configure_libcxx.py --out $(LIBCPP_GENERATED)
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
$(BUILD)/userland/cxx_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_test.elf
	python3 tools/verify_libcxx.py
test: test-libcxx
.PHONY: libcxx test-libcxx
$(BUILD)/userland/cxx_guard_test.o: userland/examples/cxx_guard_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_guard_test.d
$(BUILD)/userland/cxx_guard_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_guard_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_guard_test.elf

$(BUILD)/userland/cxx23_test.o: userland/examples/cxx_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx23_test.d
$(BUILD)/userland/cxx23_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx23_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx23_test.elf
