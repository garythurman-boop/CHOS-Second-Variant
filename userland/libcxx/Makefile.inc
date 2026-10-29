# Pinned real libc++ allocation/algorithm sources and bounded container profile.
LIBCPP_ROOT := third_party/libcxx
LIBCPP_GENERATED := $(BUILD)/userland/libcxx/include
LIBCPP_CONFIG := $(LIBCPP_GENERATED)/__config_site $(LIBCPP_GENERATED)/__assertion_handler
LIBCPP_HEADERS := $(shell rg --files $(LIBCPP_ROOT) userland/libcxx/include)
# C++ wrappers must precede C headers, including for their include_next contracts.
LIBCPP_INCLUDES := -nostdinc++ -isystem $(LIBCPP_GENERATED) -isystem userland/libcxx/include -isystem $(LIBCPP_ROOT)/include -isystem userland/libc/include -isystem $(MUSL_MATH_GENERATED)
LIBCPP_FLAGS := $(filter-out -std=c11 -mgeneral-regs-only,$(USER_CFLAGS)) -std=c++20 -fno-exceptions -fno-rtti -femulated-tls -ffunction-sections -fdata-sections $(LIBCPP_INCLUDES)
LIBCPP_SOURCES := $(LIBCPP_ROOT)/src/charconv.cpp $(LIBCPP_ROOT)/src/ryu/d2fixed.cpp $(LIBCPP_ROOT)/src/ryu/d2s.cpp $(LIBCPP_ROOT)/src/ryu/f2s.cpp $(LIBCPP_ROOT)/src/new.cpp $(LIBCPP_ROOT)/src/new_handler.cpp $(LIBCPP_ROOT)/src/new_helpers.cpp $(LIBCPP_ROOT)/src/algorithm.cpp $(LIBCPP_ROOT)/src/hash.cpp userland/libcxx/string_instantiations.cpp userland/libcxx/verbose_abort.cpp userland/libcxx/threading.cpp userland/libcxx/clocks.cpp $(LIBCPP_ROOT)/src/mutex.cpp $(LIBCPP_ROOT)/src/mutex_destructor.cpp $(LIBCPP_ROOT)/src/condition_variable.cpp $(LIBCPP_ROOT)/src/condition_variable_destructor.cpp $(LIBCPP_ROOT)/src/memory.cpp $(LIBCPP_ROOT)/src/thread.cpp $(LIBCPP_ROOT)/src/future.cpp userland/libcxx/termination.cpp
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
	python3 tools/verify_libcxxabi.py
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

$(BUILD)/userland/cxx_sync_test.o: userland/examples/cxx_sync_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_sync_test.d
$(BUILD)/userland/cxx_sync_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_sync_test.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_sync_test.elf

ICU_THREAD_ROOT := third_party/icu-threading
ICU_THREAD_HEADERS := $(shell rg --files $(ICU_THREAD_ROOT))
ICU_THREAD_FLAGS := $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -DU_STATIC_IMPLEMENTATION=1 -DU_DEBUG=1 -I$(ICU_THREAD_ROOT)/source/common
$(BUILD)/userland/icu_umutex.o: $(ICU_THREAD_ROOT)/source/common/umutex.cpp $(ICU_THREAD_HEADERS) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_THREAD_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/userland/icu_threading_test.o: userland/examples/icu_threading_test.cpp $(ICU_THREAD_HEADERS) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_THREAD_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/icu_umutex.d $(BUILD)/userland/icu_threading_test.d
$(BUILD)/userland/icu_threading_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/icu_threading_test.o $(BUILD)/userland/icu_umutex.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-icu-threading: $(BUILD)/userland/icu_threading_test.elf
	python3 tools/verify_icu_threading.py
test: test-icu-threading
.PHONY: test-icu-threading

# Preserve upstream's warning for the standard's unknown CPU-count hint (0).
$(BUILD)/userland/libcxx/third_party/libcxx/src/thread.o: LIBCPP_FLAGS += -Wno-error=cpp

$(BUILD)/userland/cxx_thread_test.o: userland/examples/cxx_thread_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
$(BUILD)/userland/cxx_thread_postkey.o: userland/examples/cxx_thread_postkey.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_thread_test.d $(BUILD)/userland/cxx_thread_postkey.d
$(BUILD)/userland/cxx_thread_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_thread_test.o $(BUILD)/userland/cxx_thread_postkey.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_thread_test.elf

$(BUILD)/userland/cxx_thread20_test.o: userland/examples/cxx_thread_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/cxx_thread20_test.d
$(BUILD)/userland/cxx_thread20_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/cxx_thread20_test.o $(BUILD)/userland/cxx_thread_postkey.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/cxx_thread20_test.elf

# Original Itanium RTTI algorithm; exception throw/unwind support stays absent.
LIBCXXABI_ROOT := third_party/libcxxabi
RTTI_SOURCES := $(LIBCXXABI_ROOT)/src/private_typeinfo.cpp $(LIBCXXABI_ROOT)/src/stdlib_typeinfo.cpp $(LIBCXXABI_ROOT)/src/stdlib_exception.cpp $(LIBCXXABI_ROOT)/src/cxa_aux_runtime.cpp userland/libcxx/abi_abort.cpp
RTTI_OBJECTS := $(addprefix $(BUILD)/userland/libcxx/,$(addsuffix .o,$(basename $(RTTI_SOURCES))))
RTTI_HEADERS := $(shell rg --files $(LIBCXXABI_ROOT) -g '*.h')
RTTI_FLAGS := $(filter-out -fno-rtti,$(LIBCPP_FLAGS)) -frtti -I$(LIBCXXABI_ROOT)/include
$(RTTI_OBJECTS): $(RTTI_HEADERS)
$(BUILD)/userland/libcxx/third_party/libcxxabi/%.o: third_party/libcxxabi/%.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(RTTI_FLAGS) -MMD -MP -c $< -o $@
# The standalone diagnostic backend has no RTTI itself.
-include $(RTTI_OBJECTS:.o=.d)
$(LIBCPP_ARCHIVE): $(RTTI_OBJECTS)

$(BUILD)/userland/rtti_test.o: userland/examples/rtti_test.cpp userland/examples/rtti_types.h $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(RTTI_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/userland/rtti_factory.o: userland/examples/rtti_factory.cpp userland/examples/rtti_types.h $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(RTTI_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/rtti_test.d $(BUILD)/userland/rtti_factory.d
$(BUILD)/userland/rtti_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/rtti_test.o $(BUILD)/userland/rtti_factory.o $(LIBCPP_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libcxx: $(BUILD)/userland/rtti_test.elf

$(BUILD)/userland/ranges_complex_test.o: userland/examples/ranges_complex_test.cpp tests/complex_vectors.h $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -Itests -MMD -MP -c $< -o $@
$(BUILD)/userland/ranges_complex20_test.o: userland/examples/ranges_complex_test.cpp tests/complex_vectors.h $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -Itests -MMD -MP -c $< -o $@
-include $(BUILD)/userland/ranges_complex_test.d $(BUILD)/userland/ranges_complex20_test.d
$(BUILD)/userland/ranges_complex_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/ranges_complex_test.o $(LIBCPP_ARCHIVE) $(COMPLEX_RT_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
$(BUILD)/userland/ranges_complex20_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/ranges_complex20_test.o $(LIBCPP_ARCHIVE) $(COMPLEX_RT_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
test-libcxx: $(BUILD)/userland/ranges_complex_test.elf $(BUILD)/userland/ranges_complex20_test.elf

$(BUILD)/userland/wide_test.o: userland/examples/wide_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -MMD -MP -c $< -o $@
-include $(BUILD)/userland/wide_test.d
$(BUILD)/userland/wide_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/wide_test.o $(LIBCPP_ARCHIVE) $(WIDE_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
test-libcxx: $(BUILD)/userland/wide_test.elf

$(BUILD)/userland/stdio_test.o: userland/examples/stdio_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -MMD -MP -c $< -o $@
-include $(BUILD)/userland/stdio_test.d
$(BUILD)/userland/stdio_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/stdio_test.o $(LIBCPP_ARCHIVE) $(STDIO_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test: $(BUILD)/userland/stdio_test.elf
$(BUILD)/userland/ordered_test.o: userland/examples/ordered_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -c $< -o $@
$(BUILD)/userland/ordered20_test.o: userland/examples/ordered_test.cpp $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(LIBCPP_FLAGS) -c $< -o $@
$(BUILD)/userland/ordered_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/ordered_test.o $(LIBCPP_ARCHIVE) $(QUAD_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
$(BUILD)/userland/ordered20_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/ordered20_test.o $(LIBCPP_ARCHIVE) $(QUAD_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
test: $(BUILD)/userland/ordered_test.elf $(BUILD)/userland/ordered20_test.elf
$(BUILD)/userland/format_test.o: userland/examples/format_test.cpp tests/format_vectors.h $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -Itests -frounding-math -c $< -o $@
$(BUILD)/userland/format_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/format_test.o $(LIBCPP_ARCHIVE) $(FORMAT_ARCHIVE) $(QUAD_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(WIDE_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
test: $(BUILD)/userland/format_test.elf

# Real private implementation include tree, with the same C++23 preprocessing
# profile used to establish the pinned source dependency closure.
CHARCONV_ORIGINAL_HEADERS := $(wildcard $(LIBCPP_ROOT)/src/include/ryu/*.h) $(LIBCPP_ROOT)/src/include/to_chars_floating_point.h
CHARCONV_OBJECTS := $(addprefix $(BUILD)/userland/libcxx/$(LIBCPP_ROOT)/src/,charconv.o ryu/d2fixed.o ryu/d2s.o ryu/f2s.o)
$(CHARCONV_OBJECTS): LIBCPP_FLAGS := $(filter-out -std=c++20,$(LIBCPP_FLAGS)) -std=c++23 -I$(LIBCPP_ROOT)/src
$(CHARCONV_OBJECTS): $(CHARCONV_ORIGINAL_HEADERS)

test-charconv-vectors:
	python3 tests/generate_charconv_vectors.py --check
test: test-charconv-vectors
.PHONY: test-charconv-vectors
