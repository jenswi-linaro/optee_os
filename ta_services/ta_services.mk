.PHONY: all
all: ta_dev_kit

.PHONY: clean
clean:

ta_flags_32 := CROSS_COMPILE="$(CROSS_COMPILE32)" \
	    TA_DEV_KIT_DIR="$(CURDIR)/$(out-dir)/export-ta_arm32"

ta_flags_64 := CROSS_COMPILE="$(CROSS_COMPILE64)" \
	    TA_DEV_KIT_DIR="$(CURDIR)/$(out-dir)/export-ta_arm64"

# Keymaster TA
ifeq ($(CFG_TA_SERVICES_KEYMASTER),32)
build_ta_keymaster := y
ta_keymaster_flags := $(ta_flags_32)
else ifeq ($(CFG_TA_SERVICES_KEYMASTER),64)
build_ta_keymaster := y
# if core is built as 32, forcing TA to be built also as 32bit
ifneq ($(CFG_ARM64_core),y)
ta_keymaster_flags := $(ta_flags_32)
else
ta_keymaster_flags := $(ta_flags_64)
endif
else
build_ta_keymaster := n
endif

ifeq ($(build_ta_keymaster),y)
ta_keymaster_dir := keymaster

all: ta_keymaster
.PHONY: ta_keymaster
ta_keymaster: ta_dev_kit
	$(MAKE) -C ta_services/$(ta_keymaster_dir) $(ta_keymaster_flags) \
		O=$(CURDIR)/$(out-dir)/ta_services/$(ta_keymaster_dir)

clean: ta_keymaster_clean
.PHONY: ta_keymaster_clean
ta_keymaster_clean:
	$(MAKE) -C ta_services/$(ta_keymaster_dir) $(ta_keymaster_flags) \
		O=$(CURDIR)/$(out-dir)/ta_services/$(ta_keymaster_dir) clean
endif
