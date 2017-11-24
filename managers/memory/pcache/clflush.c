/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * This file describes routines for handling
 * 	pcache line flush
 */

#include <lego/fit_ibapi.h>
#include <lego/ratelimit.h>
#include <lego/comp_memory.h>
#include <lego/comp_storage.h>
#include <memory/include/vm.h>
#include <memory/include/pid.h>
#include <processor/pcache.h>

#include "internal.h"

int handle_p2m_flush_one(struct p2m_flush_payload *payload, u64 desc,
			 struct common_header *hdr)
{
	int reply;
	pid_t pid;
	unsigned long user_va;
	struct lego_task_struct *tsk;

	pid = payload->pid;
	user_va = payload->user_va;

	pcache_debug("I nid:%u tgid:%u user_va:%#lx",
		hdr->src_nid, pid, user_va);

	tsk = find_lego_task_by_pid(hdr->src_nid, pid);
	if (unlikely(!tsk)) {
		reply = -ESRCH;
		goto out_reply;
	}

	reply = RET_OKAY;
out_reply:
	ibapi_reply_message(&reply, sizeof(reply), desc);
	return 0;
}
