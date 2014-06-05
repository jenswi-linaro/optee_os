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
platform-cflags += -g -gdwarf-2
platform-aflags += -g -gdwarf-2

core-platform-subdirs += \
	$(addprefix $(arch-dir)/, kernel mm tee sta) $(platform-dir)

libutil_with_isoc := y

include mk/config.mk

CFG_TEE_CORE_EMBED_INTERNAL_TESTS?=1
core-platform-cppflags += \
	-DCFG_TEE_CORE_EMBED_INTERNAL_TESTS=$(CFG_TEE_CORE_EMBED_INTERNAL_TESTS)

core-platform-cppflags += -DTEE_USE_DLMALLOC
core-platform-cppflags += -D_USE_SLAPORT_LIB


# Several CPU suppoorted
core-platform-cppflags += -DTEE_MULTI_CPU
# define flag to support booting from GDB
core-platform-cppflags += -DCONFIG_TEE_GDB_BOOT
core-platform-cppflags += -DCFG_NO_TA_HASH_SIGN

core-platform-cppflags += -DWITH_UART_DRV=1
core-platform-cppflags += -DWITH_PLAT_H=1
