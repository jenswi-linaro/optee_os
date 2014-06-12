CROSS_PREFIX	?= arm-linux-gnueabihf
CROSS_COMPILE	?= $(CROSS_PREFIX)-
include mk/gcc.mk

platform-cpuarch = cortex-a15
platform-cflags	 = -mcpu=$(platform-cpuarch) -mthumb
platform-cflags	+= -pipe -mthumb-interwork -mlong-calls
platform-cflags += -fno-short-enums -mno-apcs-float -fno-common
platform-aflags	 = -mcpu=$(platform-cpuarch)
core-platform-cppflags	 = -I$(arch-dir)/include
core-platform-cppflags	+= -DNUM_THREADS=2
core-platform-cppflags	+= -DWITH_STACK_CANARIES=1
user_ta-platform-cflags = -fpie

DEBUG		?= 1
ifeq ($(DEBUG),1)
platform-cflags += -O0
else
platform-cflags += -Os
endif
platform-cflags += -g -g3
platform-aflags += -g -g3

core-platform-subdirs += \
	$(addprefix $(arch-dir)/, kernel mm sm tee sta) $(platform-dir)

libutil_with_isoc := y

include mk/config.mk
include $(platform-dir)/system_config.in

core-platform-cppflags += -DCFG_TEE_CORE_NB_CORE=$(CFG_TEE_CORE_NB_CORE)

CFG_TEE_CORE_EMBED_INTERNAL_TESTS?=1
core-platform-cppflags += \
	-DCFG_TEE_CORE_EMBED_INTERNAL_TESTS=$(CFG_TEE_CORE_EMBED_INTERNAL_TESTS)

core-platform-cppflags += \
	-DCFG_DDR_TEETZ_RESERVED_START=$(CFG_DDR_TEETZ_RESERVED_START) \
	-DCFG_DDR_TEETZ_RESERVED_SIZE=$(CFG_DDR_TEETZ_RESERVED_SIZE)

core-platform-cppflags += -DTEE_USE_DLMALLOC
core-platform-cppflags += -D_USE_SLAPORT_LIB


# Several CPU suppoorted
core-platform-cppflags += -DTEE_MULTI_CPU
# define flag to support booting from GDB
core-platform-cppflags += -DCONFIG_TEE_GDB_BOOT
core-platform-cppflags += -DCFG_NO_TA_HASH_SIGN

ifdef DDR_PHYS_START
core-platform-cppflags += -DCFG_DDR_START=$(DDR_PHYS_START)
core-platform-cppflags += -DCFG_DDR_SIZE=$(DDR_SIZE)
endif
ifdef DDR1_PHYS_START
core-platform-cppflags += -DCFG_DDR1_START=$(DDR1_PHYS_START)
core-platform-cppflags += -DCFG_DDR1_SIZE=$(DDR1_SIZE)
endif

core-platform-cppflags += -DSTACK_TMP_SIZE=$(STACK_TMP_SIZE)
core-platform-cppflags += -DSTACK_ABT_SIZE=$(STACK_ABT_SIZE)
core-platform-cppflags += -DSTACK_THREAD_SIZE=$(STACK_THREAD_SIZE)

core-platform-cppflags += -DWITH_UART_DRV=1
