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

#pragma once

#include <stdint.h>

#include "buxton.h"
#include "buxtonkey.h"
#include "buxtondata.h"
#include "daemon.h"

/**
 * Initialize the Cynara async client
 * @param self self buxtond instance being run
 * @return 0 on success, -1 on error
 */
int buxton_cynara_initialize(BuxtonDaemon *self);

/**
 * Release the Cynara async client
 */
void buxton_cynara_finish(void);

/**
 * Check whether fd is Cynara's or not
 * @return a boolean
 */
bool buxton_cynara_check_fd(int fd);

/**
 * Process events on Cynara client
 */
void buxton_cynara_process(void);

/**
 * Check permissions
 * @param self buxtond instance being run
 * @param client Used to validate privilege
 * @param msgid client's message ID
 * @param msg buxton control message type
 * @param key buxton key
 * @param value buxton value
 * @param permitted a boolean value indicates whether it is allowed or denied
 * @return true when permitted is set
 */
bool buxton_cynara_check(BuxtonDaemon *self, client_list_item *client,
		uint32_t msgid, BuxtonControlMessage msg, _BuxtonKey *key,
		BuxtonData *value, bool *permitted);

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
