/*
 * Copyright (c) 2026 myflavor <admin@myflv.cn>. All rights reserved.
 * Based on Re-Kernel project by nep_timeline@outlook.com.
 * File: rekernel_x_main.c — Module entry (init/exit) & hooks wiring.
 */

#include "rekernel_x_log.h"
#include "rekernel_x.h"
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/init.h>

static int __init start_rekernel(void)
{
	rekernel_x_log_info("starting...\n");
	rekernel_x_log_debug("Debug mode is enabled!\n");
	rekernel_x_log_info("Version %s |  by myflavor, Sakion Team\n", REKERNEL_X_VERSION);

	if (register_genl() != LINE_SUCCESS)
	{
		rekernel_x_log_err("%s: Failed to register genl family!\n", __func__);
		return LINE_ERROR;
	}

	rekernel_x_log_info("start hooking!\n");

	if (register_binder() != LINE_SUCCESS)
	{
		rekernel_x_log_err("%s: Failed to hook binder!\n", __func__);
		goto err;
	}

	if (register_signal() != LINE_SUCCESS)
	{
		rekernel_x_log_err("%s: Failed to hook signal!\n", __func__);
		goto err;
	}

	if (register_netfilter() != LINE_SUCCESS)
	{
		rekernel_x_log_err("%s: Failed to hook netfilter!\n", __func__);
		goto err;
	}

#ifdef CLEAN_UP_ASYNC_BINDER
	register_binder_kp();
#endif

	rekernel_x_log_info("hooked!\n");
	return LINE_SUCCESS;

err:
	unregister_binder_kp();
	unregister_netfilter();
	unregister_signal();
	unregister_binder();
	unregister_genl();
	return LINE_ERROR;
}

static void __exit exit_rekernel(void)
{
	rekernel_x_log_info("closing...\n");
	unregister_binder_kp();
	unregister_netfilter();
	unregister_signal();
	unregister_binder();
	unregister_genl();
}

module_init(start_rekernel);
module_exit(exit_rekernel);

MODULE_LICENSE("GPL");
