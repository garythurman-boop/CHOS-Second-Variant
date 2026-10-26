# Reviewed TLS1.2 prerequisite profile, separately configured from boot entropy.
TLS_MODULES := aes asn1parse asn1write base64 bignum bignum_core bignum_mod bignum_mod_raw cipher cipher_wrap constant_time ecdh ecdsa ecp ecp_curves gcm hmac_drbg md oid pk pk_ecc pk_wrap pkparse platform_util rsa rsa_alt_helpers sha256 sha512 ssl_ciphersuites ssl_client ssl_msg ssl_tls ssl_tls12_client x509 x509_crt
TLS_HEADERS := $(wildcard userland/tls/include/*.h third_party/mbedtls/include/mbedtls/*.h third_party/mbedtls/include/psa/*.h third_party/mbedtls/library/*.h)
TLS_FLAGS := $(LIBC_INCLUDES) -Iuserland/tls/include -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"chos_tls_config.h"' -ffunction-sections -fdata-sections
TLS_OBJECTS := $(addprefix $(BUILD)/userland/tls/upstream/,$(addsuffix .o,$(TLS_MODULES)))
$(BUILD)/userland/tls/upstream/%.o: third_party/mbedtls/library/%.c $(TLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(TLS_FLAGS) -MMD -MP -Wno-unused-function -c $< -o $@
-include $(TLS_OBJECTS:.o=.d)
$(BUILD)/userland/tls/%.o: userland/tls/%.c $(TLS_HEADERS) userland/libchos/include/chos/user.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(TLS_FLAGS) -c $< -o $@
$(BUILD)/userland/tls_test.o: userland/examples/tls_test.c $(TLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(TLS_FLAGS) -c $< -o $@
$(BUILD)/userland/tls_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/native_abi.o $(BUILD)/userland/tls_test.o $(BUILD)/userland/tls/client.o $(BUILD)/userland/tls/platform.o $(TLS_OBJECTS) $(LIBC_OBJECTS) userland/tls/native.ld
	$(LD) -T userland/tls/native.ld -nostdlib -static --gc-sections -z max-page-size=4096 -o $@ $(filter %.o,$^)
TLS_HOST_FLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -DCHOS_TLS_HOST_TEST -Iuserland/tls/include -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE='"chos_tls_config.h"' -ffunction-sections -fdata-sections
TLS_UPSTREAM_C := $(addprefix third_party/mbedtls/library/,$(addsuffix .c,$(TLS_MODULES)))
$(BUILD)/test-tls-x509: tests/test_tls_x509.c $(TLS_UPSTREAM_C) $(TLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(TLS_HOST_FLAGS) -O2 -Wl,--gc-sections $< $(TLS_UPSTREAM_C) -o $@
$(SAN_DIR)/test-tls-x509: tests/test_tls_x509.c $(TLS_UPSTREAM_C) $(TLS_HEADERS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(TLS_HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wl,--gc-sections $< $(TLS_UPSTREAM_C) -o $@
test-tls: $(BUILD)/test-tls-x509 $(BUILD)/userland/tls_test.elf
	python3 tests/run_tls_x509.py $(BUILD)/test-tls-x509
test-tls-sanitize: $(SAN_DIR)/test-tls-x509
	ASAN_OPTIONS=detect_leaks=1 python3 tests/run_tls_x509.py $(SAN_DIR)/test-tls-x509
test: test-tls
test-sanitize: test-tls-sanitize
.PHONY: test-tls test-tls-sanitize
