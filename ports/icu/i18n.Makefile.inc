ICU_I18N_ROOT := third_party/icu-i18n/source/i18n
ICU_I18N_NAMES := $(shell python3 -c 'import json;print(" ".join(json.load(open("docs/dependencies/icu-i18n.json"))["sources"]))')
ICU_I18N_HEADERS := $(shell rg --files $(ICU_I18N_ROOT) | rg '\.h$$')
ICU_I18N_DECIMAL := $(BUILD)/icu-i18n/decContext.h
ICU_I18N_OBJECTS := $(addprefix $(BUILD)/icu-i18n/,$(ICU_I18N_NAMES:.cpp=.o))
ICU_I18N_ARCHIVE := $(BUILD)/icu-i18n/libicui18n.a
ICU_I18N_FLAGS := $(filter-out -DU_COMMON_IMPLEMENTATION=1,$(ICU_COMMON_FLAGS)) -DU_I18N_IMPLEMENTATION=1 -I$(ICU_I18N_ROOT) -include $(ICU_I18N_DECIMAL)
$(ICU_I18N_DECIMAL): $(ICU_I18N_ROOT)/decContext.h $(ICU_I18N_ROOT)/decContext.cpp docs/dependencies/icu-i18n.json tools/configure_icu_i18n.py
	python3 tools/configure_icu_i18n.py --out $@
$(BUILD)/icu-i18n/%.o: $(ICU_I18N_ROOT)/%.cpp $(ICU_I18N_HEADERS) $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(ICU_I18N_DECIMAL) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_I18N_FLAGS) -Wno-unused-parameter -Wno-sign-compare -MMD -MP -c $< -o $@
-include $(ICU_I18N_OBJECTS:.o=.d)
$(ICU_I18N_ARCHIVE): $(ICU_I18N_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
icu-i18n: $(ICU_I18N_ARCHIVE)
	python3 tools/verify_icu_i18n.py
.PHONY: icu-i18n
$(BUILD)/userland/icu_i18n_test.o: userland/examples/icu_i18n_test.cpp $(ICU_I18N_HEADERS) $(ICU_COMMON_HEADERS) $(ICU_COMMON_CONFIG) $(ICU_I18N_DECIMAL) $(LIBCPP_CONFIG) $(LIBCPP_HEADERS) $(LIBC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(ICU_I18N_FLAGS) -Werror -Wno-deprecated-declarations -c $< -o $@
$(BUILD)/userland/icu_i18n_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/icu_i18n_test.o $(ICU_I18N_ARCHIVE) $(ICU_COMMON_ARCHIVE) $(ICU_BOOTSTRAP_DATA) $(LIBCPP_ARCHIVE) $(STDIO_ARCHIVE) $(FORMAT_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(QUAD_ARCHIVE) $(WIDE_ARCHIVE) $(FLOAT_SCAN_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^) --start-group $(filter %.a,$^) --end-group
$(BUILD)/userland/icu_i18n_test.app.elf: $(BUILD)/userland/icu_i18n_test.elf
	llvm-objcopy --strip-debug $< $@
	python3 -c 'import pathlib;assert pathlib.Path("$@").stat().st_size<=2*1024*1024+4096'
icu-i18n-test: $(BUILD)/userland/icu_i18n_test.app.elf
	python3 tools/verify_icu_i18n.py
.PHONY: icu-i18n-test

test: icu-i18n-test
