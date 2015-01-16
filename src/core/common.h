/*
 * This file is part of buxton.
 *
 * Copyright (C) 2015 Samsung Electronics
 *
 * buxton is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */
#pragma once

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <sys/socket.h>

#include "buxton.h"
#include "buxtonkey.h"
#include "buxtonstring.h"

/**
 * Possible security decisions
 */
typedef enum BuxtonRequestDecision {
	BUXTON_DECISION_NONE,
	BUXTON_DECISION_REQUIRED,
	BUXTON_DECISION_GRANTED,
	BUXTON_DECISION_DENIED
} BuxtonRequestDecision;

/**
 * List for daemon's clients
 */
typedef struct client_list_item {
	LIST_FIELDS(struct client_list_item, item); /**<List type */
	int fd; /**<File descriptor of connected client */
	struct ucred cred; /**<Credentials of connected client */
	BuxtonString *smack_label; /**<Smack label of connected client */
	uint8_t *data; /**<Data buffer for the client */
	size_t offset; /**<Current position to write to data buffer */
	size_t size; /**<Size of the data buffer */
} client_list_item;

/**
 * Represents client's request to daemon
 */
typedef struct BuxtonRequest {
	client_list_item *client; /**<Client */
	BuxtonControlMessage type; /**<Type of message in the response */
	uint32_t msgid; /**<Message identifier */
	_BuxtonKey *key; /**<Key used by client to make the request */
	BuxtonRequestDecision is_group_permitted; /**<Decision about group permission */
	BuxtonRequestDecision is_key_permitted; /**<Decision about key permission */
} BuxtonRequest;
