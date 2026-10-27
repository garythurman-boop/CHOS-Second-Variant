# Pinned musl binary32/binary64 math. Fenv selects real ARM64 assembly.
MUSL_MATH_ROOT := third_party/musl
MUSL_MATH_GENERATED := $(BUILD)/userland/libm/include
MUSL_MATH_CFLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -D_XOPEN_SOURCE=700 -ffunction-sections -fdata-sections -frounding-math -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-command-line-argument
MUSL_MATH_AR ?= llvm-ar
MUSL_MATH_INCLUDES := -I$(MUSL_MATH_ROOT)/arch/aarch64 -I$(MUSL_MATH_ROOT)/arch/generic -I$(MUSL_MATH_ROOT)/src/include -I$(MUSL_MATH_ROOT)/src/internal -I$(MUSL_MATH_GENERATED) -I$(MUSL_MATH_ROOT)/include
MUSL_MATH_SOURCES := $(shell python3 -c 'import json;print(" ".join("third_party/musl/"+p for p in json.load(open("docs/dependencies/musl-math.json"))["sources"]))')
MUSL_MATH_HEADERS := $(shell rg --files $(MUSL_MATH_ROOT) -g '*.h')
MUSL_MATH_HOST_SOURCES := $(shell python3 -c 'import json;print(" ".join("third_party/musl/"+p for p in json.load(open("docs/dependencies/musl-math.json"))["host_sources"]))')
MUSL_MATH_OBJECTS := $(addprefix $(BUILD)/userland/libm/,$(addsuffix .o,$(basename $(MUSL_MATH_SOURCES))))
MUSL_MATH_ARCHIVE := $(BUILD)/userland/libm/libchos-m.a
MUSL_MATH_PUBLIC := $(addprefix $(MUSL_MATH_GENERATED)/,math.h fenv.h features.h bits/fenv.h bits/alltypes.h)
$(MUSL_MATH_GENERATED)/math.h: $(MUSL_MATH_ROOT)/include/math.h
	@mkdir -p $(dir $@)
	cp $< $@
$(MUSL_MATH_GENERATED)/fenv.h: $(MUSL_MATH_ROOT)/include/fenv.h
	@mkdir -p $(dir $@)
	cp $< $@
$(MUSL_MATH_GENERATED)/features.h: $(MUSL_MATH_ROOT)/include/features.h
	@mkdir -p $(dir $@)
	cp $< $@
$(MUSL_MATH_GENERATED)/bits/fenv.h: $(MUSL_MATH_ROOT)/arch/aarch64/bits/fenv.h
	@mkdir -p $(dir $@)
	cp $< $@
$(MUSL_MATH_GENERATED)/bits/alltypes.h: $(MUSL_MATH_ROOT)/arch/aarch64/bits/alltypes.h.in $(MUSL_MATH_ROOT)/include/alltypes.h.in $(MUSL_MATH_ROOT)/tools/mkalltypes.sed
	@mkdir -p $(dir $@)
	sed -f $(MUSL_MATH_ROOT)/tools/mkalltypes.sed $(MUSL_MATH_ROOT)/arch/aarch64/bits/alltypes.h.in $(MUSL_MATH_ROOT)/include/alltypes.h.in > $@
$(BUILD)/userland/libm/%.o: %.c $(MUSL_MATH_HEADERS) $(MUSL_MATH_GENERATED)/bits/alltypes.h
	@mkdir -p $(dir $@)
	$(CC) $(MUSL_MATH_CFLAGS) $(MUSL_MATH_INCLUDES) -MMD -MP -c $< -o $@
$(BUILD)/userland/libm/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(MUSL_MATH_CFLAGS) -c $< -o $@
-include $(MUSL_MATH_OBJECTS:.o=.d)
$(MUSL_MATH_ARCHIVE): $(MUSL_MATH_OBJECTS)
	rm -f $@
	$(MUSL_MATH_AR) rcs $@ $^
libm: $(MUSL_MATH_ARCHIVE) $(MUSL_MATH_PUBLIC)
.PHONY: libm
$(BUILD)/userland/libm_test.o: userland/examples/libm_test.c tests/libm_vectors.h $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -frounding-math $(LIBC_INCLUDES) -I$(MUSL_MATH_GENERATED) -Itests -c $< -o $@
$(BUILD)/userland/libm_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/libm_test.o $(LIBC_OBJECTS) $(MUSL_MATH_ARCHIVE) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test: $(BUILD)/userland/libm_test.elf

$(BUILD)/libm-host/libhost-checked-m.a: tools/build_math_host.py docs/dependencies/musl-math.json $(MUSL_MATH_HOST_SOURCES) $(MUSL_MATH_HEADERS)
	python3 tools/build_math_host.py --cc $(CC_HOST) --out $(BUILD)/libm-host
$(SAN_DIR)/libm-host/libhost-checked-m.a: tools/build_math_host.py docs/dependencies/musl-math.json $(MUSL_MATH_HOST_SOURCES) $(MUSL_MATH_HEADERS)
	python3 tools/build_math_host.py --cc $(CC_HOST) --out $(SAN_DIR)/libm-host --sanitize
$(BUILD)/test-libm: tests/test_libm.c tests/libm_vectors.h $(BUILD)/libm-host/libhost-checked-m.a
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin -frounding-math tests/test_libm.c $(BUILD)/libm-host/libhost-checked-m.a -lm -o $@
$(SAN_DIR)/test-libm: tests/test_libm.c tests/libm_vectors.h $(SAN_DIR)/libm-host/libhost-checked-m.a
	$(CC_HOST) $(SAN_FLAGS) -fno-builtin -frounding-math tests/test_libm.c $(SAN_DIR)/libm-host/libhost-checked-m.a -lm -o $@
test-libm: $(BUILD)/test-libm $(BUILD)/userland/libm_test.elf
	python3 tools/verify_musl_math.py
	./$(BUILD)/test-libm
test-libm-sanitize: $(SAN_DIR)/test-libm
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_DIR)/test-libm
test: test-libm
test-sanitize: test-libm-sanitize
.PHONY: test-libm test-libm-sanitize

# Original compiler-rt binary128 arithmetic, using real AArch64 FPCR/FPSR.
QUAD_ROOT := third_party/compiler-rt
QUAD_SOURCES := $(shell python3 -c 'import json;print(" ".join("third_party/compiler-rt/"+p for p in json.load(open("docs/dependencies/compiler-rt-quad.json"))["sources"]))')
QUAD_OBJECTS := $(addprefix $(BUILD)/userland/libquad/,$(addsuffix .o,$(basename $(QUAD_SOURCES))))
QUAD_HEADERS := $(shell rg --files $(QUAD_ROOT) -g '*.h' -g '*.inc')
QUAD_ARCHIVE := $(BUILD)/userland/libquad/libchos-quad.a
$(BUILD)/userland/libquad/%.o: %.c $(QUAD_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(LIBC_INCLUDES) -ffunction-sections -fdata-sections -MMD -MP -c $< -o $@
-include $(QUAD_OBJECTS:.o=.d)
$(QUAD_ARCHIVE): $(QUAD_OBJECTS)
	rm -f $@
	llvm-ar rcs $@ $^
libquad: $(QUAD_ARCHIVE)
.PHONY: libquad
$(BUILD)/userland/quad_test.o: userland/examples/quad_test.c tests/quad_vectors.h $(LIBC_HEADERS) $(MUSL_MATH_PUBLIC)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) $(LIBC_INCLUDES) -I$(MUSL_MATH_GENERATED) -Itests -frounding-math -MMD -MP -c $< -o $@
-include $(BUILD)/userland/quad_test.d
$(BUILD)/userland/quad_test.elf: $(BUILD)/userland/start.o $(NATIVE_STATIC_START) $(BUILD)/userland/native_abi.o $(BUILD)/userland/quad_test.o $(QUAD_ARCHIVE) $(MUSL_MATH_ARCHIVE) $(LIBC_OBJECTS) userland/app.ld
	$(LD) -T userland/app.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o %.a,$^)
test-libquad: $(BUILD)/userland/quad_test.elf
	python3 tools/verify_compiler_rt.py
	python3 tools/generate_quad_vectors.py --check
test: test-libquad
.PHONY: test-libquad
