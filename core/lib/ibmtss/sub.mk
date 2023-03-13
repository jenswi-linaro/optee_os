global-incdirs-y += include
incdirs-y += internal_inc

cflags-lib-y += -Wno-strict-aliasing
cflags-lib-y += -Wno-switch-default
cflags-lib-y += -Wno-cast-align
cflags-lib-y += -Wno-unused-parameter
srcs-y += src/tss.c
srcs-y += src/tssutils.c
srcs-y += src/tssproperties.c
srcs-y += src/tssresponsecode.c
srcs-y += src/tssauth.c
srcs-y += src/tsstransmit.c
srcs-y += src/tss20.c
srcs-y += src/tssmarshal.c
srcs-y += src/Unmarshal.c
srcs-y += src/tssauth20.c
srcs-y += src/tssprint.c
srcs-y += src/tssprintcmd.c
srcs-y += src/tssccattributes.c
srcs-y += src/Commands.c
srcs-y += src/CommandAttributeData.c
srcs-y += src/tsscryptoh.c

incdirs-y += src
srcs-y += tsscrypto_optee.c
srcs-y += tssdev_optee.c
