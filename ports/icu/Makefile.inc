ICU_COMMON_ROOT := third_party/icu-common/source/common
ICU_COMMON_NAMES := $(shell python3 -c 'import json;print(" ".join(json.load(open("docs/dependencies/icu-common.json"))["sources"]))')
ICU_COMMON_HEADERS := $(shell rg --files $(ICU_COMMON_ROOT) | rg '\.h$$')
ICU_COMMON_CONFIG := ports/icu/native_config.h
ICU_COMMON_PATCHED := $(BUILD)/icu-common/putil.cpp
ICU_COMMON_OBJECTS := $(addprefix $(BUILD)/icu-common/,$(ICU_COMMON_NAMES:.cpp=.o))
ICU_COMMON_ARCHIVE := $(BUILD)/icu-common/libicuuc.a
ICU_COMMON_FLAGS := $(filter-out -std=c++20 -Werror,$(LIBCPP_FLAGS)) -std=c++23 -frtti -fhosted -DU_COMMON_IMPLEMENTATION=1 -DU_DEBUG=1 -include $(ICU_COMMON_CONFIG) -I$(ICU_COMMON_ROOT)
$(ICU_COMMON_PATCHED): $(ICU_COMMON_ROOT)/putil.cpp docs/dependencies/icu-common.json tools/configure_icu_common.py
	python3 tools/configure_icu_common.py --out $@
$(BUILD)/icu-common/%.o: $(ICU_COMMON_ROOT)/%.cpp $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_COMMON_FLAGS) -Wno-unused-parameter -Wno-sign-compare -MMD -MP -c $< -o $@
$(BUILD)/icu-common/putil.o: $(ICU_COMMON_PATCHED) $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	$(CC) $(ICU_COMMON_FLAGS) -Wno-unused-parameter -Wno-sign-compare -MMD -MP -c $(ICU_COMMON_PATCHED) -o $@
-include $(ICU_COMMON_OBJECTS:.o=.d)
$(ICU_COMMON_ARCHIVE): $(ICU_COMMON_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
icu-common: $(ICU_COMMON_ARCHIVE)
	python3 tools/verify_icu_common.py
.PHONY: icu-common

# Original upstream data-tool bootstrap only. This is NOT libicudata and is
# never staged as a complete SDK data dependency or counted as engine readiness.
ICU_BOOTSTRAP_DATA := $(BUILD)/icu-common/libicustubdata.a
$(BUILD)/icu-common/stubdata.o: third_party/icu-common/source/stubdata/stubdata.cpp third_party/icu-common/source/stubdata/stubdata.h $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	$(CC) $(ICU_COMMON_FLAGS) -DU_DATA_IMPLEMENTATION=1 -c $< -o $@
$(ICU_BOOTSTRAP_DATA): $(BUILD)/icu-common/stubdata.o
	rm -f $@
	llvm-ar rcs $@ $^
icu-common: $(ICU_BOOTSTRAP_DATA)
$(BUILD)/userland/icu_common_test.o: userland/examples/icu_common_test.cpp tests/icu_fold_vectors.h $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_COMMON_FLAGS) -Werror -Itests -c $< -o $@
$(BUILD)/userland/icu_common_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/icu_common_test.o $(ICU_COMMON_ARCHIVE) $(ICU_BOOTSTRAP_DATA) $(LIBCPP_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(COMPLEX_RT_ARCHIVE) $(QUAD_ARCHIVE) $(WIDE_ARCHIVE) $(STDIO_ARCHIVE) $(FLOAT_SCAN_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
icu-common-test: $(BUILD)/userland/icu_common_test.elf
.PHONY: icu-common-test

$(BUILD)/userland/icu_common_test.app.elf: $(BUILD)/userland/icu_common_test.elf
	llvm-objcopy --strip-debug $< $@
	python3 -c 'import pathlib; p=pathlib.Path("$@"); assert p.stat().st_size <= 2*1024*1024+4096, "native file limit"'
icu-common-test: $(BUILD)/userland/icu_common_test.app.elf
	python3 tools/verify_icu_common.py
	python3 tests/generate_icu_fold_vectors.py --check
test: icu-common-test
