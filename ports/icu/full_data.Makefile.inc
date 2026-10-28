ICU_FULL_DATA_DIR := $(BUILD)/icu-data
ICU_FULL_DATA_ARCHIVE := $(ICU_FULL_DATA_DIR)/libicudata.a
ICU_FULL_DATA_INPUTS := third_party/icu-data-source/icu79-data-source.tar.gz third_party/icu-data-source/LICENSE docs/dependencies/icu-data-source.json docs/dependencies/icu-generated-data.json tools/build_icu_data.py tools/verify_icu_data_source.py
$(ICU_FULL_DATA_ARCHIVE): $(ICU_FULL_DATA_INPUTS)
	python3 tools/build_icu_data.py --out $(ICU_FULL_DATA_DIR)
icu-full-data: $(ICU_FULL_DATA_ARCHIVE)
	python3 tools/verify_icu_full_data.py --build-dir $(ICU_FULL_DATA_DIR)
.PHONY: icu-full-data
test-icu-data-package: icu-full-data
	python3 tests/test_icu_data_package.py $(ICU_FULL_DATA_DIR)
.PHONY: test-icu-data-package
test: test-icu-data-package
