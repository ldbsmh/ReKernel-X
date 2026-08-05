rekernel_x-y := rkx.o rkx_genl.o rkx_netuid.o rkx_free_async.o rkx_frozen.o rkx_binder.o rkx_signal.o rkx_netfilter.o rkx_binder_kp.o rkx_binder_alloc.o

obj-m += rekernel_x.o

# Avoid unresolved __stack_chk_guard / __stack_chk_fail on kernels that do not
# export stack protector symbols to loadable modules.
ccflags-remove-y += -fstack-protector
ccflags-remove-y += -fstack-protector-strong
ccflags-remove-y += -fstack-protector-all
ccflags-y += -fno-stack-protector
subdir-ccflags-y += -fno-stack-protector

CFLAGS_rkx.o += -fno-stack-protector
CFLAGS_rkx_genl.o += -fno-stack-protector
CFLAGS_rkx_netuid.o += -fno-stack-protector
CFLAGS_rkx_free_async.o += -fno-stack-protector
CFLAGS_rkx_frozen.o += -fno-stack-protector
CFLAGS_rkx_binder.o += -fno-stack-protector
CFLAGS_rkx_signal.o += -fno-stack-protector
CFLAGS_rkx_netfilter.o += -fno-stack-protector
CFLAGS_rkx_binder_kp.o += -fno-stack-protector
CFLAGS_rkx_binder_alloc.o += -fno-stack-protector

ifdef RKX_VERSION
ccflags-y += -DRKX_VERSION=\"$(RKX_VERSION)\"
endif
