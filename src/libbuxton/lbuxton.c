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

/**
 * \file lbuxton.c Buxton library implementation
 */
#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>

#include "buxton.h"
#include "buxtonclient.h"
#include "buxtonkey.h"
#include "buxtonresponse.h"
#include "buxtonstring.h"
#include "configurator.h"
#include "hashmap.h"
#include "log.h"
#include "protocol.h"
#include "util.h"

static Hashmap *key_hash = NULL;

int buxton_set_conf_file(const char *path)
{
	int r;
	struct stat st;

	r = stat(path, &st);
	if (r == -1) {
		return errno;
	} else {
		if (st.st_mode & S_IFDIR) {
			return EINVAL;
		}
	}

	buxton_add_cmd_line(CONFIG_CONF_FILE, path);

	return 0;
}

int buxton_open(BuxtonClient *client)
{
	_BuxtonClient **c = (_BuxtonClient **)client;
	_BuxtonClient *cl = NULL;
	int bx_socket, r;
	struct sockaddr_un remote;
	size_t sock_name_len;

	if ((bx_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		return -1;
	}

	remote.sun_family = AF_UNIX;
	sock_name_len = strlen(buxton_socket()) + 1;
	if (sock_name_len >= sizeof(remote.sun_path)) {
		buxton_log("Provided socket name: %s is too long, maximum allowed length is %d bytes\n",
			   buxton_socket(), sizeof(remote.sun_path));
		return -1;
	}

	strncpy(remote.sun_path, buxton_socket(), sock_name_len);
	r = connect(bx_socket, (struct sockaddr *)&remote, sizeof(remote));
	if ( r == -1) {
		close(bx_socket);
		return -1;
	}

	if (fcntl(bx_socket, F_SETFL, O_NONBLOCK)) {
		close(bx_socket);
		return -1;
	}

	if (!setup_callbacks()) {
		close(bx_socket);
		return -1;
	}

	cl = malloc0(sizeof(_BuxtonClient));
	if (!cl) {
		close(bx_socket);
		return -1;
	}

	cl->fd = bx_socket;
	*c = cl;

	return bx_socket;
}

void buxton_close(BuxtonClient client)
{
	_BuxtonClient *c;
	BuxtonKey key = NULL;
	Iterator i;

	/* Free all remaining allocated keys */
	HASHMAP_FOREACH_KEY(key, key, key_hash, i) {
		hashmap_remove_value(key_hash, key, key);
		buxton_key_free(key);
	}

	hashmap_free(key_hash);

	key_hash = NULL;

	if (!client) {
		return;
	}

	c = (_BuxtonClient *)client;

	cleanup_callbacks();
	close(c->fd);
	c->direct = 0;
	c->fd = -1;
	free(c);
}

int buxton_get_value(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !(k->group.value) || !(k->name.value) ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX) {
		return EINVAL;
	}

	r = buxton_wire_get_value((_BuxtonClient *)client, k, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_register_notification(BuxtonClient client,
				 BuxtonKey key,
				 BuxtonCallback callback,
				 void *data,
				 bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->name.value ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX) {
		return EINVAL;
	}

	r = buxton_wire_register_notification((_BuxtonClient *)client, k,
					      callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_unregister_notification(BuxtonClient client,
				   BuxtonKey key,
				   BuxtonCallback callback,
				   void *data,
				   bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->name.value ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX) {
		return EINVAL;
	}

	r = buxton_wire_unregister_notification((_BuxtonClient *)client, k,
						callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_set_value(BuxtonClient client,
		     BuxtonKey key,
		     const void *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->name.value || !k->layer.value ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX ||
	    k->type == BUXTON_TYPE_UNSET || !value) {
		return EINVAL;
	}

	r = buxton_wire_set_value((_BuxtonClient *)client, k, value, callback,
				  data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

static int buxton_set_privileges(BuxtonClient client,
		     BuxtonKey key,
		     const char *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync,
		     BuxtonControlMessage msg)
{
	bool r;
	int ret = 0;
	BuxtonString v;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->layer.value || !value) {
		return EINVAL;
	}
	if (msg != BUXTON_CONTROL_SET_PRIV
			&& msg != BUXTON_CONTROL_SET_READ_PRIV
			&& msg != BUXTON_CONTROL_SET_WRITE_PRIV) {
		return EINVAL;
	}

	/* discarding const until BuxtonString updated */
	v = buxton_string_pack((char*)value);

	r = buxton_wire_set_priv((_BuxtonClient *)client, k, &v, callback,
				  data, msg);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_set_privilege(BuxtonClient client,
		     BuxtonKey key,
		     const char *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_set_privileges(client, key, value, callback, data, sync,
			BUXTON_CONTROL_SET_PRIV);
}

int buxton_set_read_privilege(BuxtonClient client,
		     BuxtonKey key,
		     const char *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_set_privileges(client, key, value, callback, data, sync,
			BUXTON_CONTROL_SET_READ_PRIV);
}

int buxton_set_write_privilege(BuxtonClient client,
		     BuxtonKey key,
		     const char *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_set_privileges(client, key, value, callback, data, sync,
			BUXTON_CONTROL_SET_WRITE_PRIV);
}

int buxton_set_label(BuxtonClient client,
		     BuxtonKey key,
		     const char *value,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_set_privilege(client, key, value, callback, data, sync);
}

static int buxton_get_privileges(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync,
		     BuxtonControlMessage msg)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->layer.value ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX) {
		return EINVAL;
	}
	if (msg != BUXTON_CONTROL_GET_PRIV
			&& msg != BUXTON_CONTROL_GET_READ_PRIV
			&& msg != BUXTON_CONTROL_GET_WRITE_PRIV) {
		return EINVAL;
	}

	r = buxton_wire_get_priv((_BuxtonClient *)client, k, callback, data, msg);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_get_privilege(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_get_privileges(client, key, callback, data, sync,
			BUXTON_CONTROL_GET_PRIV);
}

int buxton_get_read_privilege(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_get_privileges(client, key, callback, data, sync,
			BUXTON_CONTROL_GET_READ_PRIV);
}
int buxton_get_write_privilege(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_get_privileges(client, key, callback, data, sync,
			BUXTON_CONTROL_GET_WRITE_PRIV);
}

int buxton_get_label(BuxtonClient client,
		     BuxtonKey key,
		     BuxtonCallback callback,
		     void *data,
		     bool sync)
{
	return buxton_get_privilege(client, key, callback, data, sync);
}

int buxton_create_group(BuxtonClient client,
			BuxtonKey key,
			BuxtonCallback callback,
			void *data,
			bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	/* We require the key name to be NULL, since it is not used for groups */
	if (!k || !k->group.value || k->name.value || !k->layer.value) {
		return EINVAL;
	}

	r = buxton_wire_create_group((_BuxtonClient *)client, k, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_remove_group(BuxtonClient client,
			BuxtonKey key,
			BuxtonCallback callback,
			void *data,
			bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	/* We require the key name to be NULL, since it is not used for groups */
	if (!k || !k->group.value || k->name.value || !k->layer.value) {
		return EINVAL;
	}

	r = buxton_wire_remove_group((_BuxtonClient *)client, k, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_client_list_keys(BuxtonClient client,
			    const char *layer_name,
			    BuxtonCallback callback,
			    void *data,
			    bool sync)
{
	bool r;
	int ret = 0;
	BuxtonString l;

	if (!layer_name) {
		return EINVAL;
	}

	/* discarding const until BuxtonString is updated */
	l = buxton_string_pack((char*)layer_name);

	r = buxton_wire_list_keys((_BuxtonClient *)client, &l, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_list_names(BuxtonClient client,
			    const char *layer_name,
			    const char *group_name,
			    const char *prefix_filter,
			    BuxtonCallback callback,
			    void *data,
			    bool sync)
{
	bool r;
	int ret = 0;
	BuxtonString l;
	BuxtonString g;
	BuxtonString p;

	if (!layer_name) {
		return EINVAL;
	}

	/* discarding const until BuxtonString is updated */
	l = buxton_string_pack((char*)layer_name);

	if (group_name) {
		g = buxton_string_pack((char*)group_name); /* discarding const is okay */
	} else {
		g.value = NULL;
		g.length = 0;
	}

	if (prefix_filter) {
		p = buxton_string_pack((char*)prefix_filter); /* discarding const is okay */
	} else {
		p.value = NULL;
		p.length = 0;
	}

	r = buxton_wire_list_names((_BuxtonClient *)client, &l, &g, &p, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

int buxton_unset_value(BuxtonClient client,
		       BuxtonKey key,
		       BuxtonCallback callback,
		       void *data,
		       bool sync)
{
	bool r;
	int ret = 0;
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k || !k->group.value || !k->name.value || !k->layer.value ||
	    k->type <= BUXTON_TYPE_MIN || k->type >= BUXTON_TYPE_MAX) {
		return EINVAL;
	}

	r = buxton_wire_unset_value((_BuxtonClient *)client, k, callback, data);
	if (!r) {
		return -1;
	}

	if (sync) {
		ret = buxton_wire_get_response(client);
		if (ret <= 0) {
			ret = -1;
		} else {
			ret = 0;
		}
	}

	return ret;
}

BuxtonKey buxton_key_create(const char *group, const char *name,
			    const char *layer, BuxtonDataType type)
{
	_BuxtonKey *key = NULL;
	char *g = NULL;
	char *n = NULL;
	char *l = NULL;

	if (!group) {
		goto fail;
	}

	if (type <= BUXTON_TYPE_MIN || type >= BUXTON_TYPE_MAX) {
		goto fail;
	}
	if (!name && type != BUXTON_TYPE_STRING && type != BUXTON_TYPE_UNSET) {
		goto fail;
	}

	if (!key_hash) {
		/* Create on hashmap on first call to key_create */
		key_hash = hashmap_new(trivial_hash_func, trivial_compare_func);
		if (!key_hash) {
			return NULL;
		}
	}

	g = strdup(group);
	if (!g) {
		goto fail;
	}

	if (name) {
		n = strdup(name);
		if (!n) {
			goto fail;
		}
	}

	if (layer) {
		l = strdup(layer);
		if (!l) {
			goto fail;
		}
	}

	key = malloc0(sizeof(_BuxtonKey));
	if (!key) {
		goto fail;
	}

	key->group.value = g;
	key->group.length = (uint32_t)strlen(g) + 1;
	if (name) {
		key->name.value = n;
		key->name.length = (uint32_t)strlen(n) + 1;
	} else {
		key->name.value = NULL;
		key->name.length = 0;
	}
	if (layer) {
		key->layer.value = l;
		key->layer.length = (uint32_t)strlen(l) + 1;
	} else {
		key->layer.value = NULL;
		key->layer.length = 0;
	}
	key->type = type;

	/* Add new keys to internal hash for cleanup on close */
	hashmap_put(key_hash, key, key);

	return (BuxtonKey)key;

fail:
	free(g);
	free(n);
	free(l);
	return NULL;
}

char *buxton_key_get_group(BuxtonKey key)
{
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!key) {
		return NULL;
	}

	return get_group(k);
}

char *buxton_key_get_name(BuxtonKey key)
{
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!key) {
		return NULL;
	}

	return get_name(k);
}

char *buxton_key_get_layer(BuxtonKey key)
{
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!key) {
		return NULL;
	}

	return get_layer(k);
}

BuxtonDataType buxton_key_get_type(BuxtonKey key)
{
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!key) {
		return -1;
	}

	return k->type;
}

void buxton_key_free(BuxtonKey key)
{
	_BuxtonKey *k = (_BuxtonKey *)key;

	if (!k) {
		return;
	}

	hashmap_remove_value(key_hash, key, key);

	free(k->group.value);
	free(k->name.value);
	free(k->layer.value);
	free(k);
}

ssize_t buxton_client_handle_response(BuxtonClient client)
{
	return buxton_wire_handle_response((_BuxtonClient *)client);
}

BuxtonControlMessage buxton_response_type(BuxtonResponse response)
{
	_BuxtonResponse *r = (_BuxtonResponse *)response;

	if (!response) {
		return -1;
	}

	return r->type;
}

int32_t buxton_response_status(BuxtonResponse response)
{
	BuxtonData *d;
	_BuxtonResponse *r = (_BuxtonResponse *)response;

	if (!response) {
		return -1;
	}

	if (buxton_response_type(response) == BUXTON_CONTROL_CHANGED) {
		return 0;
	}

	d = buxton_array_get(r->data, 0);

	if (d) {
		return d->store.d_int32;
	}

	return -1;
}

BuxtonKey buxton_response_key(BuxtonResponse response)
{
	_BuxtonKey *key = NULL;
	_BuxtonResponse *r = (_BuxtonResponse *)response;

	if (!response) {
		return NULL;
	}

	if (buxton_response_type(response) == BUXTON_CONTROL_LIST_NAMES) {
		return NULL;
	}

	key = malloc0(sizeof(_BuxtonKey));
	if (!key) {
		return NULL;
	}

	if (!buxton_key_copy(r->key, key)) {
		free(key);
		return NULL;
	}

	return (BuxtonKey)key;
}

void *buxton_response_value(BuxtonResponse response)
{
	void *p = NULL;
	BuxtonData *d = NULL;
	_BuxtonResponse *r = (_BuxtonResponse *)response;
	BuxtonControlMessage type;

	if (!response) {
		return NULL;
	}

	type = buxton_response_type(response);
	if (type == BUXTON_CONTROL_GET
			|| type == BUXTON_CONTROL_GET_PRIV
			|| type == BUXTON_CONTROL_GET_READ_PRIV
			|| type == BUXTON_CONTROL_GET_WRITE_PRIV) {
		d = buxton_array_get(r->data, 1);
	} else if (type == BUXTON_CONTROL_CHANGED) {
		if (r->data->len) {
			d = buxton_array_get(r->data, 0);
		}
	} else {
		goto out;
	}

	if (!d) {
		goto out;
	}

	switch (d->type) {
	case BUXTON_TYPE_STRING:
		return strdup(d->store.d_string.value);
	case BUXTON_TYPE_INT32:
		p = malloc0(sizeof(int32_t));
		if (!p) {
			goto out;
		}
		*(int32_t *)p = (int32_t)d->store.d_int32;
		break;
	case BUXTON_TYPE_UINT32:
		p = malloc0(sizeof(uint32_t));
		if (!p) {
			goto out;
		}
		*(uint32_t *)p = (uint32_t)d->store.d_uint32;
		break;
	case BUXTON_TYPE_INT64:
		p = malloc0(sizeof(int64_t));
		if (!p) {
			goto out;
		}
		*(int64_t *)p = (int64_t)d->store.d_int64;
		break;
	case BUXTON_TYPE_UINT64:
		p = malloc0(sizeof(uint64_t));
		if (!p) {
			goto out;
		}
		*(uint64_t *)p = (uint64_t)d->store.d_uint64;
		break;
	case BUXTON_TYPE_FLOAT:
		p = malloc0(sizeof(float));
		if (!p) {
			goto out;
		}
		*(float *)p = (float)d->store.d_float;
		break;
	case BUXTON_TYPE_DOUBLE:
		p = malloc0(sizeof(double));
		if (!p) {
			goto out;
		}
		*(double *)p = (double)d->store.d_double;
		break;
	case BUXTON_TYPE_BOOLEAN:
		p = malloc0(sizeof(bool));
		if (!p) {
			goto out;
		}
		*(bool *)p = (bool)d->store.d_boolean;
		break;
	default:
		break;
	}

out:
	return p;
}

BuxtonDataType buxton_response_value_type(BuxtonResponse response)
{
	BuxtonData *d = NULL;
	_BuxtonResponse *r = (_BuxtonResponse *)response;
	BuxtonControlMessage type;

	if (!response) {
		return -1;
	}

	type = buxton_response_type(response);
	if (type == BUXTON_CONTROL_GET
			|| type == BUXTON_CONTROL_GET_PRIV
			|| type == BUXTON_CONTROL_GET_READ_PRIV
			|| type == BUXTON_CONTROL_GET_WRITE_PRIV) {
		d = buxton_array_get(r->data, 1);
	} else if (type == BUXTON_CONTROL_CHANGED) {
		if (r->data->len) {
			d = buxton_array_get(r->data, 0);
		}
	} else {
		return -1;
	}

	if (!d) {
		return -1;
	}

	return d->type;
}

uint32_t buxton_response_list_names_count(BuxtonResponse response)
{
	_BuxtonResponse *r = (_BuxtonResponse *)response;
	BuxtonControlMessage type;

	if (!response) {
		return 0;
	}

	type = buxton_response_type(response);
	if (type != BUXTON_CONTROL_LIST_NAMES) {
		return 0;
	}
	return r->data->len ? ((uint32_t)r->data->len - 1) : 0;
}

char *buxton_response_list_names_item(BuxtonResponse response, uint32_t index)
{
	_BuxtonResponse *r = (_BuxtonResponse *)response;
	BuxtonControlMessage type;
	BuxtonData *d;

	if (!response) {
		return NULL;
	}

	type = buxton_response_type(response);
	if (type != BUXTON_CONTROL_LIST_NAMES) {
		return NULL;
	}
	if (index + 1 >= r->data->len) {
		return NULL;
	}
	d = buxton_array_get(r->data, (uint16_t)(index + 1));
	if (d == NULL) {
		return NULL;
	}
	if (d->type != BUXTON_TYPE_STRING) {
		return NULL;
	}
	return strdup(d->store.d_string.value);
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
