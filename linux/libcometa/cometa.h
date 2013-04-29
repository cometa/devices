/*
 * 	Cometa is a cloud infrastructure for embedded systems and connected 
 *  devices developed by Visible Energy, Inc.
 *
 *	Copyright (C) 2013, Visible Energy, Inc.
 * 
 *	Licensed under the Apache License, Version 2.0 (the "License");
 * 	you may not use this file except in compliance with the License.
 * 	You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 */

/* @file
 * Cometa client library include file for vanilla linux systems.
 *
 * Note: the library is installing a custom handler for signal(SIGALRM).
 */

/* 
 * Result codes for Cometa functions 
 */
typedef enum  {
	COMEATAR_OK,			/* Success */
	COMEATAR_TIMEOUT,		/* Time out before the request has completed */
	COMEATAR_NET_ERROR,		/* Network error */
	COMEATAR_HTTP_ERROR,	/* HTTP error */
	COMETAR_AUTH_ERROR,		/* Authentication error */
} cometa_reply;

/** Cometa API functions **/

/*
 * All the function of this library are synchronous, that is they block until
 * the requested operation is completed or an error or timeout occurred.
 *
 */

/*
 * Initialize the application to use Cometa and provides the necessary parameters
 * to identify the device. The device ID is in @device_id and the key in @device_key.
 * The optional parameter @platform is a string (max 64 chars) describing the device
 * platform and used only as information for device management and analytics.
 */
cometa_reply cometa_init(const char *device_id, const char *platform, const char *device_key);

/* 
 * Subscribe the device to the application @app_name at the application server with IP address
 * specified in @app_server_ip and using the key provided in @app_key.
 */
cometa_reply cometa_subscribe(const char *app_server_ip, const char *app_name, const char *app_key);

