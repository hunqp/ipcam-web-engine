/*
 * Copyright (C) 2016-2024  Davidson Francis <davidsondfgl@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#define _POSIX_C_SOURCE 200809L
#include "sha1.h"
#include "base64.h"
#include "websockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/**
 * @dir src/
 * @brief Handshake routines directory
 *
 * @file Handshake.c
 * @brief Handshake routines.
 */

/**
 * @brief Gets the field Sec-WebSocket-Accept on response, by
 * an previously informed key.
 *
 * @param configKey Sec-WebSocket-Key
 * @param dest source to be stored the value.
 *
 * @return Returns 0 if success and a negative number
 * otherwise.
 *
 * @attention This is part of the internal API and is documented just
 * for completeness.
 */
int GetHandshakeAccept(char *configKey, unsigned char **dest) {
	unsigned char hash[sha1HASH_SIZE]; /* SHA-1 Hash */
	Sha1Context_t ctx; /* SHA-1 Context */
	char *str; /* WebSocket key + magic string */

	/* Invalid key. */
	if (!configKey)
		return (-1);

	str = calloc(1, sizeof(char) * (configKEY_LEN + configMS_LEN + 1));
	if (!str)
		return (-1);

	strncpy(str, configKey, configKEY_LEN);
	strcat(str, configMAGIC_STRING);

	Sha1Reset(&ctx);
	Sha1Input(&ctx, (const uint8_t *)str, configKEYMS_LEN);
	Sha1Result(&ctx, hash);

	*dest = Base64Encode(hash, sha1HASH_SIZE, NULL);
	*(*dest + strlen((const char *)*dest) - 1) = '\0';
	free(str);
	return (0);
}

/**
 * @brief Finds the ocorrence of @p needle in @p haystack, case
 * insensitive.
 *
 * @param haystack Target string to be searched.
 * @param needle   Substring to search for.
 *
 * @returns If found, returns a pointer at the beginning of the
 * found substring. Otherwise, returns NULL.
 */
static const char *StrStrICase(const char *haystack, const char *needle) {
	size_t length;
	for (length = strlen(needle); *haystack; haystack++)
		if (!strncasecmp(haystack, needle, length))
			return haystack;
	return (NULL);
}

/**
 * @brief Gets the complete response to accomplish a succesfully
 * Handshake.
 *
 * @param hsrequest  Client request.
 * @param hsresponse Server response.
 *
 * @return Returns 0 if success and a negative number
 * otherwise.
 *
 * @attention This is part of the internal API and is documented just
 * for completeness.
 */
int GetHandshakeResponse(char *hsrequest, char **hsresponse) {
	unsigned char *accept; /* Accept message.     */
	char *saveptr;         /* strtok_r() pointer. */
	char *s;               /* Current string.     */
	int ret;               /* Return value.       */

	saveptr = NULL;
	for (s = strtok_r(hsrequest, "\r\n", &saveptr); s != NULL;
		 s = strtok_r(NULL, "\r\n", &saveptr)) {
		if (StrStrICase(s, configHANDSHAKE_REQ) != NULL)
			break;
	}

	/* Ensure that we have a valid pointer. */
	if (s == NULL)
		return (-1);

	saveptr = NULL;
	s = strtok_r(s, " ", &saveptr);
	s = strtok_r(NULL, " ", &saveptr);

	ret = GetHandshakeAccept(s, &accept);
	if (ret < 0)
		return (ret);

	*hsresponse = malloc(sizeof(char) * configHANDSHAKE_ACCLEN);
	if (*hsresponse == NULL)
		return (-1);

	strcpy(*hsresponse, configHANDSHAKE_ACCEPT);
	strcat(*hsresponse, (const char *)accept);
	strcat(*hsresponse, "\r\n\r\n");

	free(accept);
	return (0);
}
