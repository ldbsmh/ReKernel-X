rekernel_x-y := \
	rkx.o \
	rkx_genl.o \
	rkx_netuid.o \
	rkx_frozen.o \
	rkx_binder.o \
	rkx_signal.o \
	rkx_netfilter.o \
	rkx_binder_kp.o

obj-m += rekernel_x.o

#
# Disable stack protector for this external kernel module.
#
# Some target Android kernels do not export __stack_chk_guard or
# __stack_chk_fail to loadable modules. Building the module with stack
# protector enabled may therefore cause insmod to fail with:
#
#   Unknown symbol __stack_chk_guard
#

ccflags-remove-y += -fstack-protector
ccflags-remove-y += -fstack-protector-strong
ccflags-remove-y += -fstack-protector-all
ccflags-y += -fno-stack-protector

CFLAGS_rkx.o += -fno-stack-protector
CFLAGS_rkx_genl.o += -fno-stack-protector
CFLAGS_rkx_netuid.o += -fno-stack-protector
CFLAGS_rkx_frozen.o += -fno-stack-protector
CFLAGS_rkx_binder.o += -fno-stack-protector
CFLAGS_rkx_signal.o += -fno-stack-protector
CFLAGS_rkx_netfilter.o += -fno-stack-protector
CFLAGS_rkx_binder_kp.o += -fno-stack-protector

ifdef RKX_VERSION
ccflags-y += -DRKX_VERSION=\"$(RKX_VERSION)\"
endif
