rekernel_x-y := rekernel_x_main.o rekernel_x_genl.o rekernel_x_netuid.o rekernel_x_frozen.o rekernel_x_binder.o rekernel_x_signal.o rekernel_x_netfilter.o rekernel_x_binder_kp.o

obj-m += rekernel_x.o

# Disable stack protector for this external LKM.
#
# Some Android kernels do not export __stack_chk_guard / __stack_chk_fail
# to loadable kernel modules. If this module is built with stack protector
# enabled, insmod may fail with:
#
#   Unknown symbol __stack_chk_guard
#
# Force disabling stack protector avoids generating references to these
# symbols in rekernel_x.ko.
ccflags-remove-y += -fstack-protector
ccflags-remove-y += -fstack-protector-strong
ccflags-remove-y += -fstack-protector-all

ccflags-y += -fno-stack-protector
subdir-ccflags-y += -fno-stack-protector

CFLAGS_rekernel_x_main.o += -fno-stack-protector
CFLAGS_rekernel_x_genl.o += -fno-stack-protector
CFLAGS_rekernel_x_netuid.o += -fno-stack-protector
CFLAGS_rekernel_x_frozen.o += -fno-stack-protector
CFLAGS_rekernel_x_binder.o += -fno-stack-protector
CFLAGS_rekernel_x_signal.o += -fno-stack-protector
CFLAGS_rekernel_x_netfilter.o += -fno-stack-protector
CFLAGS_rekernel_x_binder_kp.o += -fno-stack-protector

ifdef REKERNEL_X_VERSION
ccflags-y += -DREKERNEL_X_VERSION=\"$(REKERNEL_X_VERSION)\"
endif
