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

#include <sys/types.h>
#include <pwd.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <cynara-client-async.h>

#include "cynara.h"
#include "buxtonkey.h"
#include "buxtondata.h"
#include "hashmap.h"
#include "util.h"
#include "daemon.h"
#include "log.h"
#include "direct.h"

#define BUXTON_CYNARA_ALLOWED 1
#define BUXTON_CYNARA_DENIED  0
#define BUXTON_CYNARA_UNKNOWN -1
#define BUXTON_CYNARA_WAITING -2

/**
 * Client's request
 */
struct BuxtonRequest {
	BuxtonDaemon *daemon;
	client_list_item *client;
	uint32_t msgid;
	BuxtonControlMessage type;
	_BuxtonKey *key;
	BuxtonData *value;
	int key_perm;
	cynara_check_id key_id;
};

static cynara_async *cynara;
static Hashmap *requests;
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
		buxton_log("Abnormal Cynara status: %d\n", status);
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

static char *buxton_cynara_get_priv_str(BuxtonControlMessage msg,
		BuxtonString *priv_read, BuxtonString *priv_write)
{
	char *str;
	BuxtonString *priv;

	switch (msg) {
	case BUXTON_CONTROL_SET:
	case BUXTON_CONTROL_UNSET:
		priv = priv_write;
		break;
	case BUXTON_CONTROL_GET:
	case BUXTON_CONTROL_GET_PRIV:
	case BUXTON_CONTROL_GET_READ_PRIV:
	case BUXTON_CONTROL_GET_WRITE_PRIV:
	case BUXTON_CONTROL_NOTIFY:
		priv = priv_read;
		break;
	default:
		return NULL;
	}

	str = malloc0(priv->length);
	if (!str)
		return NULL;

	/* is BuxtonString null-terminated? */
	memcpy(str, priv->value, priv->length);

	return str;
}

static char *buxton_cynara_get_priv(BuxtonControl *control,
		_BuxtonKey *key, BuxtonControlMessage msg)
{
	BuxtonString *priv_read;
	BuxtonString *priv_write;
	BuxtonData data;
	int ret;
	char *str;

	priv_read = malloc0(sizeof(BuxtonString));
	if (!priv_read)
		abort();
	priv_write = malloc0(sizeof(BuxtonString));
	if (!priv_write)
		abort();

	memzero(&data, sizeof(BuxtonData));
	ret = buxton_direct_get_value(control, key, &data,
			priv_read, priv_write);

	if (data.type == BUXTON_TYPE_STRING)
		free(data.store.d_string.value);

	if (ret) {
		free(priv_read);
		free(priv_write);
		return NULL;
	}

	str = buxton_cynara_get_priv_str(msg, priv_read, priv_write);
	buxton_debug("privilege string: '%s'\n", str ? str : "");

	string_free(priv_read);
	string_free(priv_write);

	return str;
}

static int buxton_cynara_check_cache(const char *client, const char *user, const char *priv)
{
	int ret;
	char errmsg[128];

	/* There is no privilege string, return permitted */
	if (!priv || !*priv || *priv == '#')
		return 1;

	ret = cynara_async_check_cache(cynara, client, "", user, priv);
	switch (ret) {
	case CYNARA_API_ACCESS_ALLOWED:
		ret = BUXTON_CYNARA_ALLOWED;
		break;
	case CYNARA_API_ACCESS_DENIED:
		ret = BUXTON_CYNARA_DENIED;
		break;
	case CYNARA_API_CACHE_MISS:
	default:
		ret = BUXTON_CYNARA_UNKNOWN;
		break;
	}

	errmsg[0] = '\0';
	cynara_strerror(ret, errmsg, sizeof(errmsg));
	buxton_debug("Cynara cache check: '%s' '%s' '%s': %d: %s\n",
			client, user, priv, ret, errmsg);

	return ret;
}

static void release_request(struct BuxtonRequest *req)
{
	if (!req)
		return;

	buxton_debug("BuxtonRequest %p released\n", req);

	if (cynara && req->key_perm == BUXTON_CYNARA_WAITING) {
		int r;

		r = cynara_async_cancel_request(cynara, req->key_id);
		if (r != CYNARA_API_SUCCESS) {
			char errmsg[128];

			errmsg[0] = '\0';
			cynara_strerror(r, errmsg, sizeof(errmsg));
			buxton_log("Failed to cancel request %u: %s\n",
					req->key_id, errmsg);
		}
	}

	key_free(req->key);
	data_free(req->value);
	free(req);
}

static struct BuxtonRequest *create_request(BuxtonDaemon *self,
		client_list_item *client, uint32_t msgid,
		BuxtonControlMessage msg, _BuxtonKey *key, BuxtonData *value)
{
	struct BuxtonRequest *req;
	bool r;

	req = malloc0(sizeof(struct BuxtonRequest));
	if (!req)
		return NULL;

	req->key = malloc0(sizeof(_BuxtonKey));
	if (!req->key) {
		free(req);
		return NULL;
	}

	r = buxton_key_copy(key, req->key);
	if (!r) {
		free(req->key);
		free(req);
		return NULL;
	}

	if (value) {
		req->value = malloc0(sizeof(BuxtonData));
		if (!req->value) {
			key_free(req->key);
			free(req);
			return NULL;
		}

		buxton_data_copy(value, req->value);
	}


	buxton_debug("BuxtonRequest %p allocated\n", req);

	req->daemon = self;
	req->client = client;
	req->msgid = msgid;
	req->type = msg;
	req->key_perm = BUXTON_CYNARA_UNKNOWN;

	return req;
}

static int get_answer(int resp)
{
	int ret;

	switch (resp) {
	case CYNARA_API_ACCESS_ALLOWED:
		ret = BUXTON_CYNARA_ALLOWED;
		break;
	case CYNARA_API_ACCESS_DENIED:
	default:
		ret = BUXTON_CYNARA_DENIED;
		break;
	}

	return ret;
}

static void send_error_reply(BuxtonDaemon *self, client_list_item *client,
		uint32_t msgid)
{
	BuxtonData response_data;
	BuxtonArray *out_list = NULL;
	_cleanup_free_ uint8_t *response_store = NULL;
	size_t response_len;
	bool ret;

	response_data.type = BUXTON_TYPE_INT32;
	response_data.store.d_int32 = -1;
	out_list = buxton_array_new();
	if (!out_list)
		abort();
	if (!buxton_array_add(out_list, &response_data))
		abort();

	response_len = buxton_serialize_message(&response_store,
			BUXTON_CONTROL_STATUS, msgid, out_list);
	if (response_len == 0) {
		buxton_log("Failed to serialize error response message\n");
		abort();
	}

	ret = _write(client->fd, response_store, response_len);
	if (!ret)
		buxton_log("Failed to send response\n");

	if (out_list)
		buxton_array_free(&out_list, NULL);
}

static bool check_client(BuxtonDaemon *self, client_list_item *client)
{
	client_list_item *cl = NULL;

	LIST_FOREACH(item, cl, self->client_list)
		if (cl == client)
			return true;

	return false;
}

static bool mark_answered(struct BuxtonRequest *req)
{
	bool r;
	struct BuxtonRequest *tmp;

	assert(req);

	tmp = hashmap_get(requests, req);
	if (!tmp) {
		buxton_debug("Removed request %p\n", req);
		return false;
	}

	req->key_perm = BUXTON_CYNARA_UNKNOWN;

	r = check_client(req->daemon, req->client);
	if (!r) {
		buxton_debug("Client connection is closed\n");
		hashmap_remove(requests, req);
		release_request(req);
		return false;
	}

	return true;
}

static void handle_request(struct BuxtonRequest *req)
{
	assert(req);

	hashmap_remove(requests, req);
	if (req->key_perm == BUXTON_CYNARA_DENIED) {
		send_error_reply(req->daemon, req->client, req->msgid);
		buxton_log("'%s' access '%s;%u' denied\n",
				req->key->name.value,
				req->client->smack_label->value,
				req->client->cred.uid);

	} else { /* allowed */
		buxtond_handle_queued_message(req->daemon, req->client,
				req->msgid, req->type,
				req->key, req->value);
	}
	release_request(req);
}

static void buxton_cynara_response(cynara_check_id id,
		cynara_async_call_cause cause, int resp, void *data)
{
	struct BuxtonRequest *req = data;
	bool r;

	assert(req);

	buxton_debug("check id %u, cause %d, resp %d\n", id, cause, resp);

	r = mark_answered(req);
	if (!r)
		return;

	switch (cause) {
	case CYNARA_CALL_CAUSE_ANSWER:
		req->key_perm = get_answer(resp);
		buxton_debug("key %u:%d\n", req->key_id, req->key_perm);
		handle_request(req);
		break;
	case CYNARA_CALL_CAUSE_CANCEL:
	case CYNARA_CALL_CAUSE_FINISH:
	case CYNARA_CALL_CAUSE_SERVICE_NOT_AVAILABLE:
	default:
		buxton_log("Cynara response is not an answer\n");
		send_error_reply(req->daemon, req->client, req->msgid);
		hashmap_remove(requests, req);
		release_request(req);
		break;
	}
}

static int buxton_cynara_request(BuxtonDaemon *self, client_list_item *client,
		uint32_t msgid, BuxtonControlMessage msg, _BuxtonKey *key,
		BuxtonData *value, const char *user, const char *priv)
{
	int ret;
	struct BuxtonRequest *req;
	cynara_check_id id;

	assert(self);
	assert(client);
	assert(key);
	assert(user);
	assert(priv);

	req = create_request(self, client, msgid, msg, key, value);
	if (!req) {
		buxton_log("Create request failed\n");
		return -1;
	}

	ret = cynara_async_create_request(cynara,
			client->smack_label->value, "", user, priv,
			&id, buxton_cynara_response, req);
	if (ret != CYNARA_API_SUCCESS) {
		char errmsg[128];

		errmsg[0] = '\0';
		cynara_strerror(ret, errmsg, sizeof(errmsg));
		buxton_log("Cynara async request failed: %d: %s\n", ret, errmsg);
		release_request(req);
		return -1;
	}
	req->key_perm = BUXTON_CYNARA_WAITING;
	req->key_id = id;

	hashmap_put(requests, req, req);

	return 0;
}

bool buxton_cynara_check(BuxtonDaemon *self, client_list_item *client,
		uint32_t msgid, BuxtonControlMessage msg, _BuxtonKey *key,
		BuxtonData *value, bool *permitted)
{
	_cleanup_free_ char *priv = NULL;
	int res;
	char user[16];
	int ret;

	if (!cynara) {
		buxton_log("Cynara is not initialized\n");
		*permitted = false;
		return true;
	}

	snprintf(user, sizeof(user), "%u", client->cred.uid);

	/* check key privilege */
	priv = buxton_cynara_get_priv(&self->buxton, key, msg);
	if (!priv || !*priv) {
		/* If privilege is not set */
		*permitted = true;
		return true;
	}

	res = buxton_cynara_check_cache(client->smack_label->value, user, priv);
	if (res != BUXTON_CYNARA_UNKNOWN) {
		*permitted = (res == BUXTON_CYNARA_ALLOWED);
		if (!*permitted) {
			buxton_log("'%s' access '%s;%s;%s' denied\n",
					key->name.value,
					client->smack_label->value,
					user, priv);
		}
		return true;
	}

	ret = buxton_cynara_request(self, client, msgid, msg, key, value, user,
			priv);
	if (ret) {
		*permitted = false;
		return true;
	}

	return false;
}

void buxton_cynara_cancel_requests(client_list_item *client)
{
	Iterator it;
	struct BuxtonRequest *req;

	HASHMAP_FOREACH(req, requests, it) {
		if (req->client != client)
			continue;

		hashmap_remove(requests, req);
		release_request(req);
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
	if (r != CYNARA_API_SUCCESS) {
		char errmsg[128];

		errmsg[0] = '\0';
		cynara_strerror(r, errmsg, sizeof(errmsg));
		buxton_log("Cynara events processing error: %d: %s\n", r, errmsg);
	}
}

int buxton_cynara_initialize(BuxtonDaemon *self)
{
	int r;

	if (cynara)
		return 0;

	requests = hashmap_new(trivial_hash_func, trivial_compare_func);
	if (!requests) {
		buxton_log("Hash table creation failed\n");
		return -1;
	}

	r = cynara_async_initialize(&cynara, NULL, buxton_cynara_status_cb, self);
	if (r != CYNARA_API_SUCCESS) {
		char errmsg[128];

		errmsg[0] = '\0';
		cynara_strerror(r, errmsg, sizeof(errmsg));
		buxton_log("Cynara initialize failed: %d: %s\n", r, errmsg);
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

	if (requests) {
		Iterator it;
		struct BuxtonRequest *req;
		HASHMAP_FOREACH(req, requests, it) {
			release_request(req);
		}
		hashmap_free(requests);
		requests = NULL;
	}
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
