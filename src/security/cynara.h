/*
 * This file is part of buxton.
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * buxton is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#pragma once

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include "backend.h"
#include "buxton.h"
#include "buxtonstring.h"
#include "common.h"
#include "daemon.h"


#define ACCESS_READ_STRING  ".read"
#define ACCESS_WRITE_STRING ".write"

/**
 * Represents client access to a given resource
 */
typedef enum BuxtonKeyAccessType {
	ACCESS_NONE = 0, /**<No access permitted */
	ACCESS_READ = 1 << 0, /**<Read access permitted */
	ACCESS_WRITE = 1 << 1, /**<Write access permitted */
	ACCESS_MAXACCESSTYPES = 1 << 2
} BuxtonKeyAccessType;

typedef enum BuxtonCynaraCheckType {
	BUXTON_CYNARA_CHECK_GROUP,
	BUXTON_CYNARA_CHECK_KEY
} BuxtonCynaraCheckType;

typedef struct BuxtonCynaraRequest {
	BuxtonCynaraCheckType type;
	BuxtonRequest *request;
} BuxtonCynaraRequest;

/*
 * Check whether the smack access matches the buxton client access
 * @param self pointer to BuxtonDaemon structure
 * @param client client name
 * @param *key, buxton key structure
 * @param access The buxton access type being queried (read/write)
 * @param checkType
 */
bool buxton_check_cynara_access(BuxtonDaemon *self,
        BuxtonString *client,
        BuxtonString *group_privilege,
        BuxtonString *data_privilege,
        BuxtonRequest *request,
        BuxtonKeyAccessType access);
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
