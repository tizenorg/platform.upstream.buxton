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

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <attr/xattr.h>

#include "daemon.h"
#include "direct.h"
#include "log.h"
#include "util.h"
#include "buxtonlist.h"
#include "cynara.h"

static char *notify_key_name(_BuxtonKey *key)
{
	int r;
	char *result;

	if (!*key->group.value || !*key->name.value)
		return NULL;

	r = asprintf(&result, "%s\n%s", key->group.value, key->name.value);
	if (r == -1) {
		abort();
	}
	return result;
}

bool parse_list(BuxtonControlMessage msg, size_t count, BuxtonData *list,
		_BuxtonKey *key, BuxtonData **value)
{
	switch (msg) {
	case BUXTON_CONTROL_SET:
		if (count != 4) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
		    list[2].type != BUXTON_TYPE_STRING || list[3].type == BUXTON_TYPE_MIN ||
		    list[3].type == BUXTON_TYPE_MAX) {
			return false;
		}
		key->layer = list[0].store.d_string;
		key->group = list[1].store.d_string;
		key->name = list[2].store.d_string;
		key->type = list[3].type;
		*value = &(list[3]);
		break;
	case BUXTON_CONTROL_SET_LABEL:
		if (count == 3) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
			    list[2].type != BUXTON_TYPE_STRING) {
				return false;
			}
			key->type = BUXTON_TYPE_UNSET;
			key->layer = list[0].store.d_string;
			key->group = list[1].store.d_string;
			*value = &list[2];
		} else if (count == 4) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
			    list[2].type != BUXTON_TYPE_STRING || list[3].type != BUXTON_TYPE_STRING) {
				return false;
			}
			key->type = BUXTON_TYPE_UNSET;
			key->layer = list[0].store.d_string;
			key->group = list[1].store.d_string;
			key->name = list[2].store.d_string;
			*value = &list[3];
		} else {
			return false;
		}
		break;
	case BUXTON_CONTROL_CREATE_GROUP:
		if (count != 2) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING) {
			return false;
		}
		key->type = BUXTON_TYPE_STRING;
		key->layer = list[0].store.d_string;
		key->group = list[1].store.d_string;
		break;
	case BUXTON_CONTROL_REMOVE_GROUP:
		if (count != 2) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING) {
			return false;
		}
		key->type = BUXTON_TYPE_STRING;
		key->layer = list[0].store.d_string;
		key->group = list[1].store.d_string;
		break;
	case BUXTON_CONTROL_GET:
		if (count == 4) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
			    list[2].type != BUXTON_TYPE_STRING || list[3].type != BUXTON_TYPE_UINT32) {
				return false;
			}
			key->layer = list[0].store.d_string;
			key->group = list[1].store.d_string;
			key->name = list[2].store.d_string;
			key->type = list[3].store.d_uint32;
		} else if (count == 3) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
			    list[2].type != BUXTON_TYPE_UINT32) {
				return false;
			}
			key->group = list[0].store.d_string;
			key->name = list[1].store.d_string;
			key->type = list[2].store.d_uint32;
		} else {
			return false;
		}
		break;
	case BUXTON_CONTROL_GET_LABEL:
		if (count == 3) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
			    list[2].type != BUXTON_TYPE_STRING) {
				return false;
			}
			key->type = BUXTON_TYPE_UNSET;
			key->layer = list[0].store.d_string;
			key->group = list[1].store.d_string;
			key->name = list[2].store.d_string;
		} else if (count == 2) {
			if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING) {
				return false;
			}
			key->type = BUXTON_TYPE_UNSET;
			key->layer = list[0].store.d_string;
			key->group = list[1].store.d_string;
		} else {
			return false;
		}
		break;
	case BUXTON_CONTROL_LIST:
		return false;
		if (count != 1) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING) {
			return false;
		}
		*value = &list[0];
		break;
	case BUXTON_CONTROL_LIST_NAMES:
		if (count != 3) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
		    list[2].type != BUXTON_TYPE_STRING) {
			return false;
		}
		key->layer = list[0].store.d_string;
		key->group = list[1].store.d_string;
		key->name = list[2].store.d_string;
		break;
	case BUXTON_CONTROL_UNSET:
		if (count != 4) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
		    list[2].type != BUXTON_TYPE_STRING || list[3].type != BUXTON_TYPE_UINT32) {
			return false;
		}
		key->layer = list[0].store.d_string;
		key->group = list[1].store.d_string;
		key->name = list[2].store.d_string;
		key->type = list[3].store.d_uint32;
		break;
	case BUXTON_CONTROL_NOTIFY:
		if (count != 3) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
		    list[2].type != BUXTON_TYPE_UINT32) {
			return false;
		}
		key->group = list[0].store.d_string;
		key->name = list[1].store.d_string;
		key->type = list[2].store.d_uint32;
		break;
	case BUXTON_CONTROL_UNNOTIFY:
		if (count != 3) {
			return false;
		}
		if (list[0].type != BUXTON_TYPE_STRING || list[1].type != BUXTON_TYPE_STRING ||
		    list[2].type != BUXTON_TYPE_UINT32) {
			return false;
		}
		key->group = list[0].store.d_string;
		key->name = list[1].store.d_string;
		key->type = list[2].store.d_uint32;
		break;
	default:
		return false;
	}

	return true;
}

bool buxtond_get_and_check_message(BuxtonDaemon *self, client_list_item *client, size_t size) {
	assert(self);
	assert(client);
	buxton_debug("buxtond_get_and_check_message\n");

	BuxtonControlMessage msg;
	BuxtonDataType memo_type;
	uint32_t msgid = 0;
	uid_t uid;
	//uint16_t i;
	ssize_t p_count;
	BuxtonData *list = NULL;
	BuxtonData *value = NULL;
	_BuxtonKey *key = NULL;
	BuxtonRequest *request  = NULL;

	BuxtonLayer *layer;
	BuxtonConfig *config;

	_cleanup_buxton_data_ BuxtonData *d = NULL;
	_cleanup_buxton_data_ BuxtonData *g = NULL;
	_cleanup_buxton_key_ _BuxtonKey *group = NULL;
	_cleanup_buxton_string_ BuxtonString *data_privilege = NULL;
	_cleanup_buxton_string_ BuxtonString *group_privilege = NULL;
	bool ret = false;
	bool waitForCynara = false;

	key = malloc0(sizeof(_BuxtonKey));
	if (!key) {
		abort();
	}

	group = malloc0(sizeof(_BuxtonKey));
	if (!group) {
		abort();
	}

	request = malloc0(sizeof(BuxtonRequest));
	if (!request) {
		abort();
	}

	g = malloc0(sizeof(BuxtonData));
	if (!g) {
		abort();
	}
	group_privilege = malloc0(sizeof(BuxtonString));
	if (!group_privilege) {
		abort();
	}

	d = malloc0(sizeof(BuxtonData));
	if (!d) {
		abort();
	}
	data_privilege = malloc0(sizeof(BuxtonString));
	if (!data_privilege) {
		abort();
	}

	//Get message
	uid = self->buxton.client.uid;
	p_count = buxton_deserialize_message((uint8_t*)client->data, &msg, size,
						 &msgid, &list);
	if (p_count < 0) {
		if (errno == ENOMEM) {
			abort();
		}
		/* Todo: terminate the client due to invalid message */
		buxton_debug("Failed to deserialize message\n");
		goto end;
	}

	/* Check valid range */
	if (msg <= BUXTON_CONTROL_MIN || msg >= BUXTON_CONTROL_MAX) {
		goto end;
	}

	// Parse message to key
	if (!parse_list(msg, (size_t)p_count, list, key, &value)) {
		goto end;
	}

	// Fill request structure
	request->client = client;
	request->type = msg;
	request->msgid = msgid;
	request->key = key;
	request->value = value;
	request->is_group_permitted = BUXTON_DECISION_NONE;
	request->is_key_permitted = BUXTON_DECISION_NONE;

	switch(msg) {
	case BUXTON_CONTROL_SET:
	case BUXTON_CONTROL_SET_LABEL:
	case BUXTON_CONTROL_UNSET:
	case BUXTON_CONTROL_GET:
	case BUXTON_CONTROL_GET_LABEL:
	case BUXTON_CONTROL_REMOVE_GROUP:
		// Getting privilege of group and key
		/* Groups must be created first, so bail if this key's group doesn't exist */
		if (!buxton_copy_key_group(key, group)) {
			abort();
		}

		ret = buxton_direct_get_value_for_layer(&(self->buxton), group, g, group_privilege);
		if (ret) {
			// FIXME: is passing it to request to process is ok?
			buxton_debug("Error(%d): %s\n", ret, strerror(ret));
			buxton_debug("Group %s for name %s missing for set value\n", key->group.value, key->name.value);
			goto end;
		}

		memo_type = key->type;
		key->type = BUXTON_TYPE_UNSET;
		ret = buxton_direct_get_value_for_layer(&(self->buxton), key, d, data_privilege);
		key->type = memo_type;
		if (ret == -ENOENT || ret == EINVAL) {
			// Setting value for nonexisting key
			// FIXME: What's the beahviour here?
			goto end;
		}

		// FIXME if key points to group, then we will get privilege twice
		if (!data_privilege->value) {
			free_buxton_string(&data_privilege);
			data_privilege = NULL;
		} else if (strcmp(data_privilege->value, group_privilege->value) == 0) {
			free_buxton_string(&data_privilege);
			data_privilege = NULL;
		}
		break;
	default:
		break;
	}

	switch (msg) {
	case BUXTON_CONTROL_SET:
	case BUXTON_CONTROL_SET_LABEL:
	case BUXTON_CONTROL_UNSET:

		waitForCynara = buxton_check_cynara_access(self, client->smack_label,
				group_privilege, data_privilege, request, ACCESS_WRITE);
		break;
	case BUXTON_CONTROL_GET:
	case BUXTON_CONTROL_GET_LABEL:
		waitForCynara = buxton_check_cynara_access(self, client->smack_label,
				group_privilege, data_privilege, request, ACCESS_READ);
		break;

	case BUXTON_CONTROL_REMOVE_GROUP:
		config = &self->buxton.config;

		if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
			// FIXME: what should we do? Should client get error?
			goto end;
		}

		if (layer->readonly) {
			buxton_debug("Read-ony layer!\n");
			// FIXME: what should we do? Should client get error?
			goto end;
		}
		if (layer->type == LAYER_USER) {
			waitForCynara = buxton_check_cynara_access(self, client->smack_label,
					group_privilege, NULL, request, ACCESS_WRITE);
		} else {
			waitForCynara = true;
		}
		break;
	case BUXTON_CONTROL_CREATE_GROUP:
	case BUXTON_CONTROL_LIST:
	case BUXTON_CONTROL_LIST_NAMES:
	case BUXTON_CONTROL_NOTIFY:
	case BUXTON_CONTROL_UNNOTIFY:
	default:
		waitForCynara = false;
		break;
	}
	ret = true;

end:
	if (!waitForCynara) {
		// Request can be already process, so add it to list of pending requests
		request_list_item *rl = NULL;
		buxton_debug("Adding request to process\n");
		rl = malloc0(sizeof(request_list_item));
		if (!rl)
			abort();
		rl->request = request;
		buxton_debug("Key : %p, key group: %s\n", rl->request->key, key->group.value ? key->group.value : "null");
		LIST_PREPEND(request_list_item, item, self->request_list, rl);
	}
	buxton_debug("Request needs response from cynara\n");

	/* Restore our own UID */
	self->buxton.client.uid = uid;

	/*if (list) {
		for (i=0; i < p_count; i++) {
			if (list[i].type == BUXTON_TYPE_STRING) {
				free(list[i].store.d_string.value);
			}
		}
		free(list);
	}*/
	return ret;
}

bool buxtond_handle_message(BuxtonDaemon *self, BuxtonControlMessage msg, uint32_t msgid,
				_BuxtonKey *key, BuxtonData *value, client_list_item *client, bool permitted)
{
	_cleanup_buxton_data_ BuxtonData *data = NULL;
	int32_t response = -1;
	uint16_t i;
	size_t response_len;
	BuxtonData response_data, mdata;
	BuxtonArray *out_list = NULL, *key_list = NULL;
	_cleanup_free_ uint8_t *response_store = NULL;
	uid_t uid;
	bool ret = false;
	uint32_t n_msgid = 0;

	assert(self);
	assert(client);

	uid = self->buxton.client.uid;
	buxton_debug("Handle client message: %p\n", key);

	/* use internal function from buxtond */
	switch (msg) {
	case BUXTON_CONTROL_SET:
		if (permitted)
			set_value(self, client, key, value, &response);
		break;
	case BUXTON_CONTROL_SET_LABEL:
		if (permitted)
			set_label(self, client, key, value, &response);
		break;
	case BUXTON_CONTROL_CREATE_GROUP:
		create_group(self, client, key, &response);
		break;
	case BUXTON_CONTROL_REMOVE_GROUP:
		if (permitted)
			remove_group(self, client, key, &response);
		break;
	case BUXTON_CONTROL_GET:
		if (permitted)
			data = get_value(self, client, key, &response);
		break;
	case BUXTON_CONTROL_GET_LABEL:
		if (permitted)
			data = get_label(self, client, key, &response);
		break;
	case BUXTON_CONTROL_UNSET:
		if (permitted)
			unset_value(self, client, key, &response);
		break;
	case BUXTON_CONTROL_LIST:
		key_list = list_keys(self, client, &value->store.d_string,
				     &response);
		break;
	case BUXTON_CONTROL_LIST_NAMES:
		key_list = list_names(self, client, key, &response);
		break;
	case BUXTON_CONTROL_NOTIFY:
		register_notification(self, client, key, msgid, &response);
		break;
	case BUXTON_CONTROL_UNNOTIFY:
		n_msgid = unregister_notification(self, client, key, &response);
		break;
	default:
		goto end;
	}

	/* Set a response code */
	response_data.type = BUXTON_TYPE_INT32;
	response_data.store.d_int32 = response;
	out_list = buxton_array_new();
	if (!out_list) {
		abort();
	}
	if (!buxton_array_add(out_list, &response_data)) {
		abort();
	}


	switch (msg) {
		/* TODO: Use cascading switch */
	case BUXTON_CONTROL_SET:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize set response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_SET_LABEL:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize set_label response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_CREATE_GROUP:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize create_group response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_REMOVE_GROUP:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize remove_group response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_GET:
		if (data && !buxton_array_add(out_list, data)) {
			abort();
		}
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize get response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_GET_LABEL:
		if (data && !buxton_array_add(out_list, data)) {
			abort();
		}
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize get_label response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_UNSET:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize unset response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_LIST:
		if (key_list) {
			for (i = 0; i < key_list->len; i++) {
				if (!buxton_array_add(out_list, buxton_array_get(key_list, i))) {
					abort();
				}
			}
			buxton_array_free(&key_list, NULL);
		}
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize list response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_LIST_NAMES:
		if (key_list) {
			for (i = 0; i < key_list->len; i++) {
				if (!buxton_array_add(out_list, buxton_array_get(key_list, i))) {
					abort();
				}
			}
			buxton_array_free(&key_list, NULL);
		}
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize list names response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_NOTIFY:
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize notify response message\n");
			abort();
		}
		break;
	case BUXTON_CONTROL_UNNOTIFY:
		mdata.type = BUXTON_TYPE_UINT32;
		mdata.store.d_uint32 = n_msgid;
		if (!buxton_array_add(out_list, &mdata)) {
			abort();
		}
		response_len = buxton_serialize_message(&response_store,
							BUXTON_CONTROL_STATUS,
							msgid, out_list);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize unnotify response message\n");
			abort();
		}
		break;
	default:
		goto end;
	}

	/* Now write the response */
	ret = _write(client->fd, response_store, response_len);
	if (ret) {
		if (msg == BUXTON_CONTROL_SET && response == 0) {
			buxtond_notify_clients(self, client, key, value);
		} else if (msg == BUXTON_CONTROL_UNSET && response == 0) {
			buxtond_notify_clients(self, client, key, NULL);
		}
	}

end:
	/* Restore our own UID */
	self->buxton.client.uid = uid;
	if (out_list) {
		buxton_array_free(&out_list, NULL);
	}
	return ret;
}

void buxtond_notify_clients(BuxtonDaemon *self, client_list_item *client,
			      _BuxtonKey *key, BuxtonData *value)
{
	BuxtonList *list = NULL;
	BuxtonList *elem = NULL;
	BuxtonNotification *nitem;
	_cleanup_free_ uint8_t* response = NULL;
	size_t response_len;
	BuxtonArray *out_list = NULL;
	_cleanup_free_ char *key_name;

	assert(self);
	assert(client);
	assert(key);

	key_name = notify_key_name(key);
	if (!key_name) {
		return;
	}
	list = hashmap_get(self->notify_mapping, key_name);
	if (!list) {
		return;
	}

	BUXTON_LIST_FOREACH(list, elem) {
		nitem = elem->data;
		int c = 1;
		__attribute__((unused)) bool unused;
		free(response);
		response = NULL;

		if (nitem->old_data && value) {
			switch (value->type) {
			case BUXTON_TYPE_STRING:
				c = memcmp((const void *)
					   (nitem->old_data->store.d_string.value),
					   (const void *)(value->store.d_string.value),
					   value->store.d_string.length);
				break;
			case BUXTON_TYPE_INT32:
				c = memcmp((const void *)&(nitem->old_data->store.d_int32),
					   (const void *)&(value->store.d_int32),
					   sizeof(int32_t));
				break;
			case BUXTON_TYPE_UINT32:
				c = memcmp((const void *)&(nitem->old_data->store.d_uint32),
					   (const void *)&(value->store.d_uint32),
					   sizeof(uint32_t));
				break;
			case BUXTON_TYPE_INT64:
				c = memcmp((const void *)&(nitem->old_data->store.d_int64),
					   (const void *)&(value->store.d_int64),
					   sizeof(int64_t));
				break;
			case BUXTON_TYPE_UINT64:
				c = memcmp((const void *)&(nitem->old_data->store.d_uint64),
					   (const void *)&(value->store.d_uint64),
					   sizeof(uint64_t));
				break;
			case BUXTON_TYPE_FLOAT:
				c = memcmp((const void *)&(nitem->old_data->store.d_float),
					   (const void *)&(value->store.d_float),
					   sizeof(float));
				break;
			case BUXTON_TYPE_DOUBLE:
				c = memcmp((const void *)&(nitem->old_data->store.d_double),
					   (const void *)&(value->store.d_double),
					   sizeof(double));
				break;
			case BUXTON_TYPE_BOOLEAN:
				c = memcmp((const void *)&(nitem->old_data->store.d_boolean),
					   (const void *)&(value->store.d_boolean),
					   sizeof(bool));
				break;
			default:
				buxton_debug("Internal state corruption: Notification data type invalid\n");
				abort();
			}
		}

		if (!c) {
			continue;
		}
		if (nitem->old_data && (nitem->old_data->type == BUXTON_TYPE_STRING)) {
			free(nitem->old_data->store.d_string.value);
		}
		free(nitem->old_data);

		nitem->old_data = malloc0(sizeof(BuxtonData));
		if (!nitem->old_data) {
			abort();
		}
		if (value) {
			if (!buxton_data_copy(value, nitem->old_data)) {
				abort();
			}
		}

		out_list = buxton_array_new();
		if (!out_list) {
			abort();
		}
		if (value) {
			if (!buxton_array_add(out_list, value)) {
				abort();
			}
		}

		response_len = buxton_serialize_message(&response,
							BUXTON_CONTROL_CHANGED,
							nitem->msgid, out_list);
		buxton_array_free(&out_list, NULL);
		if (response_len == 0) {
			if (errno == ENOMEM) {
				abort();
			}
			buxton_debug("Failed to serialize notification\n");
			abort();
		}
		buxton_debug("Notification to %d of key change (%s)\n", nitem->client->fd,
			     key_name);

		unused = _write(nitem->client->fd, response, response_len);
	}
}

void set_value(BuxtonDaemon *self, client_list_item *client, _BuxtonKey *key,
	       BuxtonData *value, int32_t *status)
{

	assert(self);
	assert(client);
	assert(key);
	assert(value);
	assert(status);

	*status = -1;

	buxton_debug("Daemon setting [%s][%s][%s]\n",
		     key->layer.value,
		     key->group.value,
		     key->name.value);

	self->buxton.client.uid = client->cred.uid;

	// FIXME : not setting privilege as client smack label
	if (!buxton_direct_set_value(&self->buxton, key, value, NULL)) {
		return;
	}

	*status = 0;
	buxton_debug("Daemon set value completed\n");
}

void set_label(BuxtonDaemon *self, client_list_item *client, _BuxtonKey *key,
	       BuxtonData *value, int32_t *status)
{

	assert(self);
	assert(client);
	assert(key);
	assert(value);
	assert(status);

	*status = -1;

	buxton_debug("Daemon setting label on [%s][%s][%s]\n",
		     key->layer.value,
		     key->group.value,
		     key->name.value);

	self->buxton.client.uid = client->cred.uid;

	/* Use internal library to set label */
	if (!buxton_direct_set_label(&self->buxton, key, &value->store.d_string)) {
		return;
	}

	*status = 0;
	buxton_debug("Daemon set label completed\n");
}

void create_group(BuxtonDaemon *self, client_list_item *client, _BuxtonKey *key,
		  int32_t *status)
{
	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	buxton_debug("Daemon creating group [%s][%s]\n",
		     key->layer.value,
		     key->group.value);

	self->buxton.client.uid = client->cred.uid;

	/* Use internal library to create group */
	// FIXME : not setting privilege as client smack label
	if (!buxton_direct_create_group(&self->buxton, key, NULL)) {
		return;
	}

	*status = 0;
	buxton_debug("Daemon create group completed\n");
}

void remove_group(BuxtonDaemon *self, client_list_item *client, _BuxtonKey *key,
		  int32_t *status)
{
	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	buxton_debug("Daemon removing group [%s][%s]\n",
		     key->layer.value,
		     key->group.value);

	self->buxton.client.uid = client->cred.uid;

	/* Use internal library to create group */
	// FIXME : not setting privilege as client smack label
	if (!buxton_direct_remove_group(&self->buxton, key)) {
		return;
	}

	*status = 0;
	buxton_debug("Daemon remove group completed\n");
}

void unset_value(BuxtonDaemon *self, client_list_item *client,
		 _BuxtonKey *key, int32_t *status)
{
	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	buxton_debug("Daemon unsetting [%s][%s][%s]\n",
		     key->layer.value,
		     key->group.value,
		     key->name.value);

	/* Use internal library to unset value */
	self->buxton.client.uid = client->cred.uid;
	// FIXME : not setting privilege as client smack label
	if (!buxton_direct_unset_value(&self->buxton, key, NULL)) {
		return;
	}

	buxton_debug("unset value returned successfully from db\n");

	*status = 0;
	buxton_debug("Daemon unset value completed\n");
}

BuxtonData *get_value(BuxtonDaemon *self, client_list_item *client,
		      _BuxtonKey *key, int32_t *status)
{
	BuxtonData *data = NULL;
	BuxtonString label;
	int32_t ret;

	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	data = malloc0(sizeof(BuxtonData));
	if (!data) {
		abort();
	}

	if (key->layer.value) {
		buxton_debug("Daemon getting [%s][%s][%s]\n", key->layer.value,
			     key->group.value, key->name.value);
	} else {
		buxton_debug("Daemon getting [%s][%s]\n", key->group.value,
			     key->name.value);
	}
	self->buxton.client.uid = client->cred.uid;
	// FIXME : not setting privilege as client smack label
	ret = buxton_direct_get_value(&self->buxton, key, data, &label);
	if (ret) {
		goto fail;
	}

	free(label.value);
	buxton_debug("get value returned successfully from db\n");

	*status = 0;
	goto end;
fail:
	buxton_debug("get value failed\n");
	free(data);
	data = NULL;
end:

	return data;
}

BuxtonData *get_label(BuxtonDaemon *self, client_list_item *client,
		      _BuxtonKey *key, int32_t *status)
{
	BuxtonData *data = NULL;
	BuxtonString label = { NULL, 0 };
	int32_t ret;

	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	data = malloc0(sizeof(BuxtonData));
	if (!data) {
		abort();
	}

	buxton_debug("Daemon getting label on [%s][%s][%s]\n",
		     key->layer.value,
		     key->group.value,
		     key->name.value);

	self->buxton.client.uid = client->cred.uid;

	// FIXME : not setting privilege as client smack label
	ret = buxton_direct_get_value(&self->buxton, key, data, &label);
	if (ret) {
		goto fail;
	}

	if (data->type == BUXTON_TYPE_STRING) {
		free(data->store.d_string.value);
	}
	data->type = BUXTON_TYPE_STRING;
	data->store.d_string = label;
	buxton_debug("get label returned successfully from db\n");

	*status = 0;
	goto end;
fail:
	buxton_debug("get label failed\n");
	free(data);
	free(label.value);
	data = NULL;
end:

	return data;
}

BuxtonArray *list_keys(BuxtonDaemon *self, client_list_item *client,
		       BuxtonString *layer, int32_t *status)
{
	BuxtonArray *ret_list = NULL;
	assert(self);
	assert(client);
	assert(layer);
	assert(status);

	*status = -1;
	if (buxton_direct_list_keys(&self->buxton, layer, &ret_list)) {
		*status = 0;
	}
	return ret_list;
}

BuxtonArray *list_names(BuxtonDaemon *self,  client_list_item *client,
			_BuxtonKey *key, int32_t *status)
{
	BuxtonArray *ret_list = NULL;
	assert(self);
	assert(client);
	assert(status);

	*status = -1;
	if (buxton_direct_list_names(&self->buxton, &key->layer, &key->group,
	    &key->name, &ret_list)) {
		*status = 0;
	}
	return ret_list;
}

void register_notification(BuxtonDaemon *self, client_list_item *client,
			   _BuxtonKey *key, uint32_t msgid,
			   int32_t *status)
{
	BuxtonList *n_list = NULL;
	BuxtonList *key_list = NULL;
	BuxtonNotification *nitem;
	BuxtonData *old_data = NULL;
	int32_t key_status;
	char *key_name;
	uint64_t *fd = NULL;
	char *key_name_copy = NULL;

	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;

	nitem = malloc0(sizeof(BuxtonNotification));
	if (!nitem) {
		abort();
	}
	nitem->client = client;

	/* Store data now, cheap */
	old_data = get_value(self, client, key, &key_status);
	if (key_status != 0) {
		free(nitem);
		return;
	}
	nitem->old_data = old_data;
	nitem->msgid = msgid;

	/* May be null, but will append regardless */
	key_name = notify_key_name(key);
	if (!key_name) {
		return;
	}

	key_name_copy = strdup(key_name);
	if (!key_name_copy) {
		abort();
	}

	n_list = hashmap_get(self->notify_mapping, key_name);
	if (!n_list) {
		if (!buxton_list_append(&n_list, nitem)) {
			abort();
		}

		if (hashmap_put(self->notify_mapping, key_name, n_list) < 0) {
			abort();
		}
	} else {
		free(key_name);
		if (!buxton_list_append(&n_list, nitem)) {
			abort();
		}
	}

	fd = malloc0(sizeof(uint64_t));
	if (!fd) {
		abort();
	}
	*fd = (uint64_t)client->fd;

	key_list = hashmap_get(self->client_key_mapping, fd);
	if (!key_list) {
		if (!buxton_list_append(&key_list, key_name_copy)) {
			abort();
		}
		if(hashmap_put(self->client_key_mapping, fd, key_list) < 0) {
			abort();
		}
	} else {
		if (!buxton_list_append(&key_list, key_name_copy)) {
			abort();
		}
		free(fd);
	}

	*status = 0;
}

uint32_t unregister_notification(BuxtonDaemon *self, client_list_item *client,
				 _BuxtonKey *key, int32_t *status)
{
	BuxtonList *plist;
	BuxtonList *n_list = NULL;
	BuxtonList *key_list = NULL;
	BuxtonList *elem = NULL;
	BuxtonNotification *nitem, *citem = NULL;
	uint32_t msgid = 0;
	_cleanup_free_ char *key_name = NULL;
	void *old_key_name;
	char *client_keyname = NULL;
	uint64_t fd = 0;
	void *old_fd = NULL;

	assert(self);
	assert(client);
	assert(key);
	assert(status);

	*status = -1;
	key_name = notify_key_name(key);
	if (!key_name) {
		return 0;
	}
	n_list = hashmap_get2(self->notify_mapping, key_name, &old_key_name);
	/* This key isn't actually registered for notifications */
	if (!n_list) {
		return 0;
	}

	BUXTON_LIST_FOREACH(n_list, elem) {
		nitem = elem->data;
		/* Find the list item for this client */
		if (nitem->client == client) {
			citem = nitem;
			break;
		}
	};

	/* Client hasn't registered for notifications on this key */
	if (!citem) {
		return 0;
	}

	fd = (uint64_t)client->fd;
	/* Remove key name from client hashmap */
	key_list = hashmap_get2(self->client_key_mapping, &fd, &old_fd);

	if (!key_list || !old_fd) {
		return 0;
	}

	BUXTON_LIST_FOREACH(key_list, elem) {
		if (!strcmp(elem->data, key_name)) {
			client_keyname = elem->data;
			break;
		}
	};

	if (client_keyname) {
		plist = key_list;
		buxton_list_remove(&key_list, client_keyname, true);
		if (!key_list) {
			hashmap_remove(self->client_key_mapping, &fd);
			free(old_fd);
		} else if (plist != key_list) {
			if (hashmap_update(self->client_key_mapping, &fd, key_list) < 0) {
				abort();
			}
		}
	}

	msgid = citem->msgid;
	/* Remove client from notifications */
	free_buxton_data(&(citem->old_data));
	plist = n_list;
	buxton_list_remove(&n_list, citem, true);

	/* If we removed the last item, remove the mapping too */
	if (!n_list) {
		(void)hashmap_remove(self->notify_mapping, key_name);
		free(old_key_name);
	} else if (plist != n_list) {
		if (hashmap_update(self->notify_mapping, key_name, n_list) < 0) {
			abort();
		}
	}

	*status = 0;

	return msgid;
}

bool identify_client(client_list_item *cl)
{
	/* Identity handling */
	ssize_t nr;
	int data;
	struct msghdr msgh;
	struct iovec iov;
	__attribute__((unused)) struct ucred *ucredp;
	struct cmsghdr *cmhp;
	socklen_t len = sizeof(struct ucred);
	int on = 1;

	assert(cl);

	union {
		struct cmsghdr cmh;
		char control[CMSG_SPACE(sizeof(struct ucred))];
	} control_un;

	setsockopt(cl->fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));

	control_un.cmh.cmsg_len = CMSG_LEN(sizeof(struct ucred));
	control_un.cmh.cmsg_level = SOL_SOCKET;
	control_un.cmh.cmsg_type = SCM_CREDENTIALS;

	msgh.msg_control = control_un.control;
	msgh.msg_controllen = sizeof(control_un.control);

	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	iov.iov_base = &data;
	iov.iov_len = sizeof(int);

	msgh.msg_name = NULL;
	msgh.msg_namelen = 0;

	nr = recvmsg(cl->fd, &msgh, MSG_PEEK | MSG_DONTWAIT);
	if (nr == -1) {
		return false;
	}

	cmhp = CMSG_FIRSTHDR(&msgh);

	if (cmhp == NULL || cmhp->cmsg_len != CMSG_LEN(sizeof(struct ucred))) {
		buxton_debug("Invalid cmessage header from kernel\n");
		abort();
	}

	if (cmhp->cmsg_level != SOL_SOCKET || cmhp->cmsg_type != SCM_CREDENTIALS) {
		buxton_debug("Missing credentials on socket\n");
		abort();
	}

	ucredp = (struct ucred *) CMSG_DATA(cmhp);

	if (getsockopt(cl->fd, SOL_SOCKET, SO_PEERCRED, &cl->cred, &len) == -1) {
		buxton_debug("Missing label on socket\n");
		abort();
	}

	return true;
}

void add_pollfd(BuxtonDaemon *self, int fd, short events, bool a)
{
	assert(self);
	assert(fd >= 0);

	if (!greedy_realloc((void **) &(self->pollfds), &(self->nfds_alloc),
			    (size_t)((self->nfds + 1) * (sizeof(struct pollfd))))) {
		abort();
	}
	if (!greedy_realloc((void **) &(self->accepting), &(self->accepting_alloc),
			    (size_t)((self->nfds + 1) * (sizeof(self->accepting))))) {
		abort();
	}
	self->pollfds[self->nfds].fd = fd;
	self->pollfds[self->nfds].events = events;
	self->pollfds[self->nfds].revents = 0;
	self->accepting[self->nfds] = a;
	self->nfds++;

	buxton_debug("Added fd %d to our poll list (accepting=%d)\n", fd, a);
}

static void del_poll_by_fd(BuxtonDaemon *self, int fd) {
	assert(self);
	nfds_t i = find_poll_fd(self, fd);
	if (i == -1)
		abort();
	del_pollfd(self, i);
}

void del_pollfd(BuxtonDaemon *self, nfds_t i)
{
	assert(self);
	assert(i < self->nfds);

	buxton_debug("Removing fd %d from our list\n", self->pollfds[i].fd);

	if (i != (self->nfds - 1)) {
		memmove(&(self->pollfds[i]),
			&(self->pollfds[i + 1]),
			/*
			 * nfds < max int because of kernel limit of
			 * fds. i + 1 < nfds because of if and assert
			 * so the casts below are always >= 0 and less
			 * than long unsigned max int so no loss of
			 * precision.
			 */
			(size_t)(self->nfds - i - 1) * sizeof(struct pollfd));
		memmove(&(self->accepting[i]),
			&(self->accepting[i + 1]),
			(size_t)(self->nfds - i - 1) * sizeof(bool));
	}
	self->nfds--;
}

nfds_t find_poll_fd(BuxtonDaemon *self, int fd) {
	nfds_t i;
	for (i = 0; i < self->nfds; i++)
		if (self->pollfds[i].fd == fd)
			return i;
	return -1;
}

void handle_smack_label(client_list_item *cl)
{
	socklen_t slabel_len = 1;
	char *buf = NULL;
	BuxtonString *slabel = NULL;
	int ret;

	ret = getsockopt(cl->fd, SOL_SOCKET, SO_PEERSEC, NULL, &slabel_len);
	/* libsmack ignores ERANGE here, so we ignore it too */
	if (ret < 0 && errno != ERANGE) {
		switch (errno) {
		case ENOPROTOOPT:
			/* If Smack is not enabled, do not set the client label */
			cl->smack_label = NULL;
			return;
		default:
			buxton_debug("getsockopt(): %m\n");
			exit(EXIT_FAILURE);
		}
	}

	slabel = malloc0(sizeof(BuxtonString));
	if (!slabel) {
		abort();
	}

	/* already checked slabel_len positive above */
	buf = malloc0((size_t)slabel_len + 1);
	if (!buf) {
		abort();
	}

	ret = getsockopt(cl->fd, SOL_SOCKET, SO_PEERSEC, buf, &slabel_len);
	if (ret < 0) {
		buxton_debug("getsockopt(): %m\n");
		exit(EXIT_FAILURE);
	}

	slabel->value = buf;
	slabel->length = (uint32_t)slabel_len;

	buxton_debug("getsockopt(): label=\"%s\"\n", slabel->value);

	cl->smack_label = slabel;
}

bool handle_client(BuxtonDaemon *self, client_list_item *cl, nfds_t i)
{
	ssize_t l;
	uint16_t peek;
	bool more_data = false;
	int message_limit = 32;

	assert(self);
	assert(cl);

	if (!cl->data) {
		cl->data = malloc0(BUXTON_MESSAGE_HEADER_LENGTH);
		cl->offset = 0;
		cl->size = BUXTON_MESSAGE_HEADER_LENGTH;
	}
	if (!cl->data) {
		abort();
	}
	/* client closed the connection, or some error occurred? */
	if (recv(cl->fd, cl->data, cl->size, MSG_PEEK | MSG_DONTWAIT) <= 0) {
		goto terminate;
	}

	/* need to authenticate the client? */
	if ((cl->cred.uid == 0) || (cl->cred.pid == 0)) {
		if (!identify_client(cl)) {
			goto terminate;
		}

		handle_smack_label(cl);
	}

	buxton_debug("New packet from UID %ld, PID %ld\n", cl->cred.uid, cl->cred.pid);

	/* Hand off any read data */
	do {
		l = read(self->pollfds[i].fd, (cl->data) + cl->offset, cl->size - cl->offset);

		/*
		 * Close clients with read errors. If there isn't more
		 * data and we don't have a complete message just
		 * cleanup and let the client resend their request.
		 */
		if (l < 0) {
			if (errno != EAGAIN) {
				goto terminate;
			} else {
				goto cleanup;
			}
		} else if (l == 0) {
			goto cleanup;
		}

		cl->offset += (size_t)l;
		if (cl->offset < BUXTON_MESSAGE_HEADER_LENGTH) {
			continue;
		}
		if (cl->size == BUXTON_MESSAGE_HEADER_LENGTH) {
			cl->size = buxton_get_message_size(cl->data, cl->offset);
			if (cl->size == 0 || cl->size > BUXTON_MESSAGE_MAX_LENGTH) {
				goto terminate;
			}
		}
		if (cl->size != BUXTON_MESSAGE_HEADER_LENGTH) {
			cl->data = realloc(cl->data, cl->size);
			if (!cl->data) {
				abort();
			}
		}
		if (cl->size > cl->offset) {
			continue;
		} else if (cl->size < cl->offset) {
			buxton_debug("Somehow read more bytes than from client requested\n");
			abort();
		}
		if (!buxtond_get_and_check_message(self, cl, cl->size)) {
			buxton_debug("Communication failed with client %d\n", cl->fd);
			goto terminate;
		}

		message_limit--;
		if (message_limit) {
			cl->size = BUXTON_MESSAGE_HEADER_LENGTH;
			cl->offset = 0;
			continue;
		}
		if (recv(cl->fd, &peek, sizeof(uint16_t), MSG_PEEK | MSG_DONTWAIT) > 0) {
			more_data = true;
		}
		goto cleanup;
	} while (l > 0);

cleanup:
	free(cl->data);
	cl->data = NULL;
	cl->size = BUXTON_MESSAGE_HEADER_LENGTH;
	cl->offset = 0;
	return more_data;

terminate:
	terminate_client(self, cl, i);
	return more_data;
}

void terminate_client(BuxtonDaemon *self, client_list_item *cl, nfds_t i)
{
	BuxtonList *key_list = NULL;
	BuxtonList *elem, *notify_elem;
	char *key_name;
	void *old_key_name = NULL;
	void *old_fd = NULL;
	uint64_t fd = (uint64_t)cl->fd;

	key_list = hashmap_get2(self->client_key_mapping, &fd, &old_fd);

	if (key_list) {
		buxton_debug("Removing notifications for client before terminating\n");
		BUXTON_LIST_FOREACH(key_list, elem) {
			key_name = elem->data;
			BuxtonList *n_list = NULL;

			n_list = hashmap_get2(self->notify_mapping, key_name, &old_key_name);
			if (!n_list || !old_key_name) {
				abort();
			}

			BuxtonNotification *nitem, *citem = NULL;

			BUXTON_LIST_FOREACH(n_list, notify_elem) {
				nitem = notify_elem->data;
				if (nitem->client == cl) {
					citem = nitem;
					break;
				}
			};

			if (!citem) {
				abort();
			}

			/* Remove client from notifications */
			free_buxton_data(&(citem->old_data));
			buxton_list_remove(&n_list, citem, true);

			/* If we removed the last item, remove the mapping too */
			if (!n_list) {
				(void)hashmap_remove(self->notify_mapping, key_name);
				free(old_key_name);
			}
		};
		/* Remove key from client hashmap */
		hashmap_remove(self->client_key_mapping, &fd);
		free(old_fd);
		buxton_list_free_all(&key_list);
	}

	del_pollfd(self, i);
	close(cl->fd);
	if (cl->smack_label) {
		free(cl->smack_label->value);
	}
	free(cl->smack_label);
	free(cl->data);
	buxton_debug("Closed connection from fd %d\n", cl->fd);
	LIST_REMOVE(client_list_item, item, self->client_list, cl);
	free(cl);
	cl = NULL;
}

void free_buxton_request(BuxtonRequest *request) {
	request->client = NULL;
	free_buxton_key(&(request->key));
	request->key = NULL;
	if (request->value && request->value->type == BUXTON_TYPE_STRING)
		free(request->value->store.d_string.value);
	free(request);
}

static short cynara_to_poll_status(cynara_async_status status) {
	switch(status) {
	case CYNARA_STATUS_FOR_READ:
		return POLLIN;
	case CYNARA_STATUS_FOR_RW:
		return POLLOUT | POLLIN;
	default:
		return POLLRDHUP;
	}
}

void buxton_cynara_status_change (int old_fd, int new_fd, cynara_async_status status,
		void *user_status_data) {
	BuxtonDaemon *self = (BuxtonDaemon *)user_status_data;
	if (old_fd == -1) {
		/* We are connecting to cynara the first time*/
		add_pollfd(self, new_fd, cynara_to_poll_status(status), false);
		self->cynara_fd = new_fd;
	} else if (new_fd == -1) {
		/* We are disconnecting from cynara*/
		del_poll_by_fd(self, old_fd);
		self->cynara_fd = -1;
	} else {
		/* Event changed or cynara reconnected*/
		del_poll_by_fd(self, old_fd);
		add_pollfd(self, new_fd, cynara_to_poll_status(status), false);
		self->cynara_fd = new_fd;
	}
}

static void set_decision(BuxtonCynaraCheckType type, BuxtonRequest *request, bool allowed) {
	switch(type) {
	case BUXTON_CYNARA_CHECK_GROUP:
		request->is_group_permitted = allowed ? BUXTON_DECISION_GRANTED : BUXTON_DECISION_DENIED;
		break;
	case BUXTON_CYNARA_CHECK_KEY:
		request->is_key_permitted = allowed ? BUXTON_DECISION_GRANTED : BUXTON_DECISION_DENIED;
		break;
	}
}

void buxton_cynara_response (cynara_check_id check_id, cynara_async_call_cause cause,
		int response, void *user_response_data) {
	BuxtonDaemon *self = (BuxtonDaemon *)user_response_data;
	BuxtonCynaraRequest *cynara_request = NULL;
	BuxtonRequest *request;
	buxton_debug("Got cynara response for %d: %d\n", check_id, response);

	if ((cynara_request = hashmap_get(self->checkid_request_mapping, &check_id)) == NULL) {
		buxton_debug("No request to cynara found\n");
		return;
	}

	request = cynara_request->request;

	switch(cause) {
	case CYNARA_CALL_CAUSE_ANSWER:
		buxton_debug("Got answer from cynara\n");
		set_decision(cynara_request->type, request, response == CYNARA_API_ACCESS_ALLOWED);
		break;
	case CYNARA_CALL_CAUSE_SERVICE_NOT_AVAILABLE:
		buxton_debug("Cynara is not available");
		set_decision(cynara_request->type, request, false);
		break;
	case CYNARA_CALL_CAUSE_CANCEL:
	case CYNARA_CALL_CAUSE_FINISH:
		hashmap_remove(self->checkid_request_mapping, &check_id);
		if (cynara_request->type == BUXTON_CYNARA_CHECK_GROUP)
			free_buxton_request(cynara_request->request);
		free(cynara_request);
		break;

	default: // This should not happened
	    set_decision(cynara_request->type, request, false);
	    break;
	}

	buxton_debug("Is group permitted: %d\n", request->is_group_permitted);
	buxton_debug("Is key permitted: %d\n", request->is_key_permitted);
	/* Permission decision for group and key is set if required */
	if (request->is_group_permitted != BUXTON_DECISION_REQUIRED &&
	    request->is_key_permitted != BUXTON_DECISION_REQUIRED) {
		buxton_debug("Request can be processed\n");
		request_list_item *rl;
		rl = malloc0(sizeof(request_list_item));
		if (!rl) {
			abort();
		}
		LIST_INIT(request_list_item, item, rl);
		rl->request = request;
		/* Buxton lacks appending to the list */
		LIST_PREPEND(request_list_item, item, self->request_list, rl);
	}
	free(cynara_request);
	hashmap_remove(self->checkid_request_mapping, &check_id);

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
