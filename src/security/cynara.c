/*
 * This file is part of buxton.
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * buxton is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <cynara-client-async.h>

#include "daemon.h"
#include "log.h"

static cynara_async *cynara;
static int cynara_fd = -1;

static short cynara_status_to_poll_event(cynara_async_status status)
{
	short evt;

	switch (status) {
	case CYNARA_STATUS_FOR_READ:
		evt = POLLIN;
		break;
	case CYNARA_STATUS_FOR_RW:
		evt = POLLIN | POLLOUT;
		break;
	default:
		evt = POLLERR | POLLHUP;
		break;
	}

	return evt;
}

static void buxton_cynara_status_cb(int old_fd, int new_fd,
		cynara_async_status status, void *data)
{
	BuxtonDaemon *self = data;
	int i;

	assert(self);

	if (old_fd != -1) {
		i = find_pollfd(self, old_fd);
		if (i != -1) {
			del_pollfd(self, (nfds_t)i);
			cynara_fd = -1;
		}
	}

	if (new_fd != -1) {
		short evt = cynara_status_to_poll_event(status);
		add_pollfd(self, new_fd, evt, false);
		cynara_fd = new_fd;
	}
}

bool buxton_cynara_check_fd(int fd)
{
	if (cynara_fd == -1)
		return false;

	return (cynara_fd == fd);
}

void buxton_cynara_process(void)
{
	int r;

	if (!cynara)
		return;

	r = cynara_async_process(cynara);
	if (r != CYNARA_API_SUCCESS)
		buxton_log("Cynara events processing error: %d\n", r);
}

int buxton_cynara_initialize(BuxtonDaemon *self)
{
	int r;

	if (cynara)
		return 0;

	r = cynara_async_initialize(&cynara, NULL, buxton_cynara_status_cb, self);
	if (r != CYNARA_API_SUCCESS) {
		buxton_log("Cynara initialize failed: %d\n", r);
		return -1;
	}

	return 0;
}

void buxton_cynara_finish(void)
{
	if (!cynara)
		return;

	cynara_async_finish(cynara);
	cynara = NULL;
	cynara_fd = -1;
}


/*
 * Editor modelines  -	http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
