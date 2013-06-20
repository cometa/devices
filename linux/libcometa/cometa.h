/*
 * @file    cometa.h
 *
 * @brief   Include file for the library to connect a linux device to the cometa infrastructure.
 *
 * Cometa is a cloud infrastructure for embedded systems and connected devices.
 *
 * Copyright (C) 2013, Visible Energy, Inc.
 * 
 */

/** Public structures and constants **/

#define DEVICE_ID_LEN   32
#define DEVICE_KEY_LEN   32
#define DEVICE_INFO_LEN 64
#define APP_NAME_LEN 32
#define APP_KEY_LEN 32
#define MESSAGE_LEN 32768

/* 
 * The opaque data structure cometa holds the context for the library
 * persistent HTTP connection and the device credentials.
 *
 * the reply attribute contains the reply response code for the last operation
 */
struct cometa;

/* 
 * Result codes for Cometa functions 
 */
typedef enum  {
	COMEATAR_OK,			/* success */
	COMEATAR_TIMEOUT,		/* time out before the request has completed */
	COMEATAR_NET_ERROR,		/* network error */
	COMEATAR_HTTP_ERROR,	/* HTTP error */
	COMETAR_AUTH_ERROR,		/* authentication error */
	COMETAR_PAR_ERROR,		/* parameters error */
	COMETAR_ERROR,			/* generic internal error */
} cometa_reply;

/* 
 * Callback to user code upon message reception. The message is released after control
 * returns to the library at the end of the callback. If the user code needs to use the 
 * content after returning, it should be copied into another buffer in the callback.
 *
 * If the callback returns a pointer to a NULL-terminated string, the string is sent back
 * to Cometa and relayed to the application server. If there is no message to send back
 * the callback returns NULL. The response message memory is not released by the library.
 *
 * @param	data_size - size of the message
 * @param	data - message
 *
 * @return - the response message to be sent to the application server
 */
typedef char *(*cometa_message_cb)(const int data_size, void *data);

/** Cometa API functions **/

/*
 * All the function of this library are synchronous, that is they block until
 * the requested operation is completed or an error or timeout occurred.
 *
 */

/*
 * Initialize the application to use Cometa and provides the necessary parameters
 * to identify the device. The device ID is in @device_id and the key in @device_key.
 * The optional parameter @platform is a string (max 64 chars [a-zA-Z] only) describing the device
 * platform and used only as information for device management and analytics.
 *
 * @return - a reply code
 *
 */

cometa_reply cometa_init(const char *device_id, const char *device_key, const char *platform);

/* 
 * Subscribe the device to the application @app_name at the application server with FQ name
 * specified in @app_server_name and using the key provided in @app_key. 
 *
 * To complete device authentication Cometa will issue an authentication request to the application
 * server endpoint specified in @auth_endpoint, with a query string containing: <device_id>, <device_key>
 * <app_key>, <challenge>. Upon successful authentication of the device in the application server, the 
 * authentication endpoint method will return a JSON object composed as follows: 
 *
 * {"response":200,"signature":"946604ed1d981eca2879:babc3d687335043f55878b3f1eef94815327d6ad533e7c7f51fb30b8ca4683a1"}
 *                              <---- app_key ----->:<----------- HMAC SHA256(challenge, app_secret) --------------->
 *
 * @return - the connection handle or NULL in case of error
 *
 */
 
struct cometa *cometa_subscribe(const char *app_name, const char *app_key, const char *app_server_name, const char *app_server_port, const char *auth_endpoint);

/*
 * Bind the @cb callback to a message received event from the connection with the specified @handle.
 * The message received by the callback is zero-terminated.
 *
 */

cometa_reply cometa_bind_cb(struct cometa *handle, cometa_message_cb cb);

/*
 * Send a message upstream to the Cometa server. 
 * 
 * The message is relayed to another server as specified in the webhook of the app registry.
 *
 */
 
cometa_reply cometa_send(struct cometa *handle, const char *buf, const int size);

/*
 * Return the last reply error in a function for the connection in @handle.
 */
cometa_reply cometa_error(struct cometa *handle);

