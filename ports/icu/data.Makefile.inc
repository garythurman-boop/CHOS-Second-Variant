ICU_SELECTED_DATA := third_party/icu-normalization-data
ICU_SELECTED_FILES := $(shell rg --files $(ICU_SELECTED_DATA))
$(BUILD)/icu-data-vectors/vectors.json: $(ICU_SELECTED_FILES) tests/generate_icu_data_vectors.py tools/verify_icu_normalization_data.py docs/dependencies/icu-normalization-data.json
	python3 tests/generate_icu_data_vectors.py --out $(BUILD)/icu-data-vectors
$(BUILD)/userland/icu_data_test.o: userland/examples/icu_data_test.cpp $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_COMMON_FLAGS) -Werror -c $< -o $@
$(BUILD)/userland/icu_data_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/icu_data_test.o $(ICU_COMMON_ARCHIVE) $(ICU_BOOTSTRAP_DATA) $(LIBCPP_ARCHIVE) $(STDIO_ARCHIVE) $(FORMAT_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(QUAD_ARCHIVE) $(WIDE_ARCHIVE) $(FLOAT_SCAN_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
$(BUILD)/userland/icu_data_test.app.elf: $(BUILD)/userland/icu_data_test.elf
	llvm-objcopy --strip-debug $< $@
	python3 -c 'import pathlib;assert pathlib.Path("$@").stat().st_size<=2*1024*1024+4096'
icu-data-test: $(BUILD)/userland/icu_data_test.app.elf $(BUILD)/icu-data-vectors/vectors.json
	python3 tools/verify_icu_normalization_data.py
.PHONY: icu-data-test
test: icu-data-test
