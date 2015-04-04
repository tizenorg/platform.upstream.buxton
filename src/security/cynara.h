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
