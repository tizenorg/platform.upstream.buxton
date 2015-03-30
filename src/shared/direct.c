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

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "direct.h"
#include "log.h"
#include "util.h"

#define BUXTON_ROOT_CHECK_ENV "BUXTON_ROOT_CHECK"

bool buxton_direct_open(BuxtonControl *control)
{

	assert(control);

	memzero(&(control->config), sizeof(BuxtonConfig));
	buxton_init_layers(&(control->config));

	control->client.direct = true;
	control->client.pid = getpid();

	return true;
}

int32_t buxton_direct_get_value(BuxtonControl *control, _BuxtonKey *key,
			     BuxtonData *data, BuxtonString *data_priv)
{
	/* Handle direct manipulation */
	BuxtonLayer *l;
	BuxtonConfig *config;
	BuxtonString layer = (BuxtonString){ NULL, 0 };
	Iterator i;
	BuxtonData d;
	int priority = 0;
	int32_t ret;
	BuxtonLayerType layer_origin = -1;

	assert(control);
	assert(key);

	if (key->layer.value) {
		ret = (int32_t)buxton_direct_get_value_for_layer(control,key, data,
						      data_priv);
		return ret;
	}

	config = &control->config;

	HASHMAP_FOREACH(l, config->layers, i) {
		key->layer.value = l->name.value;
		key->layer.length = l->name.length;
		ret = (int32_t)buxton_direct_get_value_for_layer(control,
						      key,
						      &d,
						      data_priv);
		if (!ret) {
			free(data_priv->value);
			data_priv->value = NULL;
			data_priv->length = 0;
			if (d.type == BUXTON_TYPE_STRING) {
				free(d.store.d_string.value);
			}

			if ((l->type == LAYER_SYSTEM && (layer_origin != LAYER_SYSTEM ||
							 priority <= l->priority)) ||
			    (l->type == LAYER_USER && layer_origin != LAYER_SYSTEM &&
			     priority <= l->priority)) {
				if (l->type == LAYER_SYSTEM) {
					layer_origin = LAYER_SYSTEM;
				} else {
					layer_origin = LAYER_USER;
				}
				priority = l->priority;
				layer.value = l->name.value;
				layer.length = l->name.length;
			}
		}
	}
	if (layer.value) {
		key->layer.value = layer.value;
		key->layer.length = layer.length;
		ret = (int32_t)buxton_direct_get_value_for_layer(control,
						      key,
						      data,
						      data_priv);
		key->layer.value = NULL;
		key->layer.length = 0;

		return ret;
	}
	return ENOENT;
}

int buxton_direct_get_value_for_layer(BuxtonControl *control,
				       _BuxtonKey *key,
				       BuxtonData *data,
				       BuxtonString *data_priv)
{
	/* Handle direct manipulation */
	BuxtonBackend *backend = NULL;
	BuxtonLayer *layer = NULL;
	BuxtonConfig *config;
	BuxtonData g;
	_BuxtonKey group;
	BuxtonString group_priv;
	int ret;

	assert(control);
	assert(key);
	assert(data);
	assert(data_priv);

	buxton_debug("get_value '%s:%s' for layer '%s' start\n",
		     key->group.value, key->name.value, key->layer.value);

	memzero(&g, sizeof(BuxtonData));
	memzero(&group, sizeof(_BuxtonKey));
	memzero(&group_priv, sizeof(BuxtonString));

	if (!key->layer.value) {
		ret = EINVAL;
		goto fail;
	}

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		ret = EINVAL;
		goto fail;
	}
	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;

	/* Groups must be created first, so bail if this key's group doesn't exist */
	if (key->name.value) {
		if (!buxton_copy_key_group(key, &group)) {
			abort();
		}
		ret = buxton_direct_get_value_for_layer(control, &group, &g, &group_priv);
		if (ret) {
			buxton_debug("Group %s for name %s missing for get value\n",
					key->group.value, key->name.value);
			goto fail;
		}
	}

	ret = backend->get_value(layer, key, data, data_priv);

fail:
	free(g.store.d_string.value);
	free(group.group.value);
	free(group.name.value);
	free(group.layer.value);
	free(group_priv.value);
	buxton_debug("get_value '%s:%s' for layer '%s' end\n",
		     key->group.value, key->name.value, key->layer.value);
	return ret;
}

bool buxton_direct_set_value(BuxtonControl *control,
			     _BuxtonKey *key,
			     BuxtonData *data,
			     BuxtonString *privilege)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	BuxtonString default_priv = buxton_string_pack("");
	BuxtonString *l;
	_cleanup_buxton_data_ BuxtonData *g = NULL;
	_cleanup_buxton_key_ _BuxtonKey *group = NULL;
	_cleanup_buxton_string_ BuxtonString *group_priv = NULL;
	bool r = false;
	int ret;

	assert(control);
	assert(key);
	assert(data);

	buxton_debug("set_value start\n");

	group = malloc0(sizeof(_BuxtonKey));
	if (!group) {
		abort();
	}
	g = malloc0(sizeof(BuxtonData));
	if (!g) {
		abort();
	}
	group_priv = malloc0(sizeof(BuxtonString));
	if (!group_priv) {
		abort();
	}

	/* Groups must be created first, so bail if this key's group doesn't exist */
	if (!buxton_copy_key_group(key, group)) {
		abort();
	}

	ret = buxton_direct_get_value_for_layer(control, group, g, group_priv);
	if (ret) {
		buxton_debug("Error(%d): %s\n", ret, strerror(ret));
		buxton_debug("Group %s for name %s missing for set value\n", key->group.value, key->name.value);
		goto fail;
	}

	if (privilege) {
		l = privilege;
	} else {
		l = &default_priv;
	}

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->readonly) {
		buxton_debug("Read-only layer!\n");
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;
	ret = backend->set_value(layer, key, data, l);
	if (ret) {
		buxton_debug("set value failed: %s\n", strerror(ret));
	} else {
		r = true;
	}

fail:
	buxton_debug("set_value end\n");
	return r;
}

bool buxton_direct_set_privilege(BuxtonControl *control,
			     _BuxtonKey *key,
			     BuxtonString *privilege)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	bool r = false;
	int ret;

	assert(control);
	assert(key);
	assert(privilege);

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->readonly) {
		buxton_debug("Read-only layer!\n");
		goto fail;
	}

	if (layer->type == LAYER_SYSTEM) {
		char *root_check = getenv(BUXTON_ROOT_CHECK_ENV);
		bool skip_check = (root_check && streq(root_check, "0"));

		//FIXME: should check client's capability set instead of UID
		if (control->client.uid != 0 && !skip_check) {
			buxton_debug("Not permitted to create group '%s'\n", key->group.value);
			goto fail;
		}
	} else {
		buxton_debug("Cannot set privileges in a user layer\n");
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;
	ret = backend->set_value(layer, key, NULL, privilege);
	if (ret) {
		buxton_debug("set privilege failed: %s\n", strerror(ret));
	} else {
		r = true;
	}

fail:
	return r;
}

bool buxton_direct_create_group(BuxtonControl *control,
				_BuxtonKey *key,
				BuxtonString *privilege)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	BuxtonString s, l;
	_cleanup_buxton_data_ BuxtonData *data = NULL;
	_cleanup_buxton_data_ BuxtonData *group = NULL;
	_cleanup_buxton_string_ BuxtonString *dpriv = NULL;
	_cleanup_buxton_string_ BuxtonString *gpriv = NULL;
	bool r = false;
	int ret;

	assert(control);
	assert(key);

	data = malloc0(sizeof(BuxtonData));
	if (!data) {
		abort();
	}
	group = malloc0(sizeof(BuxtonData));
	if (!group) {
		abort();
	}
	dpriv = malloc0(sizeof(BuxtonString));
	if (!dpriv) {
		abort();
	}
	gpriv = malloc0(sizeof(BuxtonString));
	if (!gpriv) {
		abort();
	}

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->readonly) {
		buxton_debug("Read-only layer!\n");
		goto fail;
	}

	if (buxton_direct_get_value_for_layer(control, key, group, gpriv) != ENOENT) {
		buxton_debug("Group '%s' already exists\n", key->group.value);
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	assert(backend);

	/* Since groups don't have a value, we create a dummy value */
	data->type = BUXTON_TYPE_STRING;
	s = buxton_string_pack("BUXTON_GROUP_VALUE");
	if (!buxton_string_copy(&s, &data->store.d_string)) {
		abort();
	}

	if (privilege) {
		if (!buxton_string_copy(privilege, dpriv)) {
			abort();
		}
	} else {
		/* "" is default privilege */
		l = buxton_string_pack("");
		if (!buxton_string_copy(&l, dpriv)) {
			abort();
		}
	}

	layer->uid = control->client.uid;
	ret = backend->set_value(layer, key, data, dpriv);
	if (ret) {
		buxton_debug("create group failed: %s\n", strerror(ret));
	} else {
		r = true;
	}

fail:
	return r;
}

bool buxton_direct_remove_group(BuxtonControl *control, _BuxtonKey *key)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	_cleanup_buxton_data_ BuxtonData *group = NULL;
	_cleanup_buxton_string_ BuxtonString *gpriv = NULL;
	bool r = false;
	int ret;

	assert(control);
	assert(key);

	group = malloc0(sizeof(BuxtonData));
	if (!group) {
		abort();
	}
	gpriv = malloc0(sizeof(BuxtonString));
	if (!gpriv) {
		abort();
	}

	config = &control->config;

	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		goto fail;
	}

	if (layer->readonly) {
		buxton_debug("Read-ony layer!\n");
		goto fail;
	}

	if (buxton_direct_get_value_for_layer(control, key, group, gpriv)) {
		buxton_debug("Group '%s' doesn't exist\n", key->group.value);
		goto fail;
	}

	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;

	ret = backend->unset_value(layer, key, NULL, NULL);
	if (ret) {
		buxton_debug("remove group failed: %s\n", strerror(ret));
	} else {
		r = true;
	}

fail:
	return r;
}

bool buxton_direct_list_keys(BuxtonControl *control,
			     BuxtonString *layer_name,
			     BuxtonArray **list)
{
	assert(control);
	assert(layer_name);

	/* Handle direct manipulation */
	BuxtonBackend *backend = NULL;
	BuxtonLayer *layer;
	BuxtonConfig *config;

	config = &control->config;
	if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
		return false;
	}
	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;
	return backend->list_keys(layer, list);
}

bool buxton_direct_list_names(BuxtonControl *control,
			     BuxtonString *layer_name,
			     BuxtonString *group,
			     BuxtonString *prefix,
			     BuxtonArray **list)
{
	/* Handle direct manipulation */
	BuxtonBackend *backend = NULL;
	BuxtonLayer *layer;
	BuxtonConfig *config;

	assert(control);
	assert(layer_name && layer_name->value);

	config = &control->config;
	if ((layer = hashmap_get(config->layers, layer_name->value)) == NULL) {
		return false;
	}
	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;
	return backend->list_names(layer, group, prefix, list);
}

bool buxton_direct_unset_value(BuxtonControl *control, _BuxtonKey *key)
{
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonConfig *config;
	_cleanup_buxton_string_ BuxtonString *data_priv = NULL;
	_cleanup_buxton_string_ BuxtonString *group_priv = NULL;
	_cleanup_buxton_data_ BuxtonData *d = NULL;
	_cleanup_buxton_data_ BuxtonData *g = NULL;
	_cleanup_buxton_key_ _BuxtonKey *group = NULL;
	int ret;
	bool r = false;

	assert(control);
	assert(key);

	group = malloc0(sizeof(_BuxtonKey));
	if (!group) {
		abort();
	}
	g = malloc0(sizeof(BuxtonData));
	if (!g) {
		abort();
	}
	group_priv = malloc0(sizeof(BuxtonString));
	if (!group_priv) {
		abort();
	}

	d = malloc0(sizeof(BuxtonData));
	if (!d) {
		abort();
	}
	data_priv = malloc0(sizeof(BuxtonString));
	if (!data_priv) {
		abort();
	}

	if (!buxton_copy_key_group(key, group)) {
		abort();
	}

	if (buxton_direct_get_value_for_layer(control, group, g, group_priv)) {
		buxton_debug("Group %s for name %s missing for unset value\n",
				key->group.value, key->name.value);
		goto fail;
	}

	if (buxton_direct_get_value_for_layer(control, key, d, data_priv)) {
		buxton_debug("Key %s not found, so unset fails\n", key->name.value);
		goto fail;
	}

	config = &control->config;
	if ((layer = hashmap_get(config->layers, key->layer.value)) == NULL) {
		return false;
	}

	if (layer->readonly) {
		buxton_debug("Read-only layer!\n");
		return false;
	}
	backend = backend_for_layer(config, layer);
	assert(backend);

	layer->uid = control->client.uid;
	ret = backend->unset_value(layer, key, NULL, NULL);
	if (ret) {
		buxton_debug("Unset value failed: %s\n", strerror(ret));
	} else {
		r = true;
	}

fail:
	return r;
}

bool buxton_direct_init_db(BuxtonControl *control, BuxtonString *layer_name)
{
	BuxtonBackend *backend;
	BuxtonConfig *config;
	BuxtonLayer *layer;
	bool ret = false;
	void *db;

	assert(control);
	assert(layer_name);

	config = &control->config;
	layer = hashmap_get(config->layers, layer_name->value);
	if (!layer) {
		goto end;
	}

	if (layer->type == LAYER_USER) {
		ret = true;
		goto end;
	}

	backend = backend_for_layer(config, layer);
	assert(backend);

	db = backend->create_db(layer);
	if (db) {
		ret = true;
	}

end:
	return ret;
}

void buxton_direct_close(BuxtonControl *control)
{
	Iterator iterator;
	BuxtonBackend *backend;
	BuxtonLayer *layer;
	BuxtonString *key;

	control->client.direct = false;

	HASHMAP_FOREACH(backend, control->config.backends, iterator) {
		destroy_backend(backend);
	}
	hashmap_free(control->config.backends);
	hashmap_free(control->config.databases);

	HASHMAP_FOREACH_KEY(layer, key, control->config.layers, iterator) {
		hashmap_remove(control->config.layers, key);
		free(layer->name.value);
		free(layer->description);
		free(layer);
	}
	hashmap_free(control->config.layers);

	control->client.direct = false;
	control->config.backends = NULL;
	control->config.databases = NULL;
	control->config.layers = NULL;
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
