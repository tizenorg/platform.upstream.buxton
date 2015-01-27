/*
 * This file is part of buxton.
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd
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
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include "cynara.h"
#include "log.h"
#include "util.h"

/**
 * This function returns pointer to combined string: "permission.request" or NULL if failed
 * Memory allocated by this function should be freed
 */
static char* combine_privilege(BuxtonString *permission, BuxtonKeyAccessType request)
{
    char *privilege = NULL;
    size_t access_size;
    char *access_type = NULL;

    switch (request) {
    case ACCESS_READ:
        access_size = strlen(ACCESS_READ_STRING);
        access_type = ACCESS_READ_STRING;
        break;
    case ACCESS_WRITE:
        access_size = strlen(ACCESS_WRITE_STRING);
        access_type = ACCESS_WRITE_STRING;
        break;
    default:
        // This should not happened
        return NULL;
    }

    privilege = malloc0(permission->length +  access_size + 1);
    if (!privilege)
        return NULL;
    memcpy(privilege, permission->value, strlen(permission->value));
    memcpy(privilege + strlen(permission->value), access_type, access_size);

    return privilege;
}

// This function should return nothing!
bool buxton_check_cynara_access(BuxtonDaemon *self,
                  BuxtonString *client,
                  BuxtonString *group_privilege,
                  BuxtonString *data_privilege,
                  BuxtonRequest *request,
                  BuxtonKeyAccessType access)
{
	int ret;
	struct passwd *pwd;
	char *key_privilege_access = NULL;
	char *group_privilege_access = NULL;
	cynara_check_id *check_id_key;
	cynara_check_id *check_id_group;
	BuxtonCynaraRequest *cynara_key = NULL;
	BuxtonCynaraRequest *cynara_group = NULL;

	check_id_key = malloc0(sizeof(cynara_check_id));
	if (!check_id_key)
		abort();

	check_id_group = malloc0(sizeof(cynara_check_id));
	if (!check_id_group)
		abort();

	request->is_group_permitted = BUXTON_DECISION_REQUIRED;
	if (data_privilege != NULL) {
		request->is_key_permitted = BUXTON_DECISION_REQUIRED;
	} else {
		request->is_key_permitted = BUXTON_DECISION_NONE;
	}

	//Get user name
	pwd = getpwuid(self->buxton.client.uid);
	if (NULL == pwd) {
		request->is_group_permitted = BUXTON_DECISION_DENIED;
		request->is_key_permitted = BUXTON_DECISION_DENIED;
		goto finish;
	}

	// Is key required and in cache?
	// Checking key access
	if (request->is_key_permitted == BUXTON_DECISION_REQUIRED) {
		//Combine permission with access type
		key_privilege_access = combine_privilege(data_privilege, access);
		if (!key_privilege_access) {
			request->is_key_permitted = BUXTON_DECISION_DENIED;
		}
		buxton_debug("Cynara cache check for: client : %s, user: %s, privilege: %s\n", client->value, pwd->pw_name, key_privilege_access);

		// Pass empty session, as cynara doesn't allow null one
		// FIXME: session probably should be a global setting
		ret = cynara_async_check_cache(self->cynara, (const char *) client->value, "",
			pwd->pw_name, key_privilege_access);

		if(CYNARA_API_ACCESS_ALLOWED == ret)
			request->is_key_permitted = BUXTON_DECISION_GRANTED;
		else if (CYNARA_API_ACCESS_DENIED == ret) {
			request->is_key_permitted = BUXTON_DECISION_DENIED;
			goto finish;
		} else if (CYNARA_API_CACHE_MISS != ret) { // Error
			request->is_key_permitted = BUXTON_DECISION_DENIED;
			goto finish;
		}
	}

	// Is group in cache?
	//Combine permission with access type
	group_privilege_access = combine_privilege(group_privilege, access);
	if (!group_privilege_access) {
		request->is_group_permitted = BUXTON_DECISION_DENIED;
	}

	ret = cynara_async_check_cache(self->cynara, (const char *) client->value, "",
			 pwd->pw_name, group_privilege_access);
	buxton_debug("Cynara cache check for: client : %s, user: %s, privilege: %s\n", client->value, pwd->pw_name, group_privilege_access);
	// Check if result has been found in cache or if error has occurred
	if(CYNARA_API_ACCESS_ALLOWED == ret) {
		request->is_group_permitted = BUXTON_DECISION_GRANTED;
	}
	else if (CYNARA_API_ACCESS_DENIED == ret) {
		request->is_group_permitted = BUXTON_DECISION_DENIED;
		goto finish;
	} else if (CYNARA_API_CACHE_MISS != ret) { //Error
		request->is_group_permitted = BUXTON_DECISION_DENIED;
		goto finish;
	}

	// Key required but not found in cache and group wasn't denied in cache
	if (request->is_key_permitted == BUXTON_DECISION_REQUIRED) {
		ret = cynara_async_create_request(self->cynara, (const char *) client->value,
				"", pwd->pw_name, key_privilege_access,
				check_id_key, &buxton_cynara_response,
				self);
		if (ret != CYNARA_API_SUCCESS) {
			// FIXME: What about CYNARA_API_MAX_PENDING_REQUESTS?
			request->is_key_permitted = BUXTON_DECISION_DENIED;
			goto finish;
		}
		cynara_key = malloc0(sizeof(BuxtonCynaraRequest));
		if (!cynara_key) {
			abort();
		}
		buxton_debug("Asking cynara with check_id: %d\n", *check_id_key);
		cynara_key->type = BUXTON_CYNARA_CHECK_KEY;
		cynara_key->request = request;
		hashmap_put(self->checkid_request_mapping, check_id_key, cynara_group);
	}
	// Group is required and key wasn't denied
	if (request->is_group_permitted == BUXTON_DECISION_REQUIRED) {
		buxton_debug("group still required\n");
		ret = cynara_async_create_request(self->cynara, (const char *)client->value,
				"", pwd->pw_name, group_privilege_access,
				check_id_group, &buxton_cynara_response,
				self);
		if (ret != CYNARA_API_SUCCESS) {
			// FIXME: What about CYNARA_API_MAX_PENDING_REQUESTS?
			// FIXME: Cancel key check request (if sent), because we failed here
			buxton_debug("cynara_async_create_request returned error: %d\n", ret);
			request->is_group_permitted = BUXTON_DECISION_DENIED;
			goto finish;
		}
		cynara_group = malloc0(sizeof(BuxtonCynaraRequest));
		if (!cynara_group) {
			abort();
		}
		buxton_debug("Asking cynara with check_id: %d\n", *check_id_group);
		cynara_group->type = BUXTON_CYNARA_CHECK_GROUP;
		cynara_group->request = request;
		hashmap_put(self->checkid_request_mapping, check_id_group, cynara_group);
	}

finish:
	free(key_privilege_access);
	free(group_privilege_access);
	//If either one was malloced, then some request was sent to cynara
	return cynara_key || cynara_group;
}
