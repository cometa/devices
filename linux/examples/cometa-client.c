/*
 * Cometa is a cloud infrastructure for embedded systems and connected 
 * devices developed by Visible Energy, Inc.
 *
 * Copyright (C) 2013, Visible Energy, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *
 * @file - Cometa client vanilla linux example.
 *
 * To use with your own application server and devices, once registered an application with Cometa and obtained
 * an application name, application key and secret, change the constant literals in "Server application details", 
 * "Cometa credentials" and "Device credentials" below.
 *
 * Build and install the libcometa library before building this example.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <cometa.h>

/* 
 * Server application details.
 *
 * APP_SERVERNAME - application server name or IP address
 * APP_SERVERNAME - server port
 * APP_ENDPOINT - authentication endpoint for devices
 *
 */
#define APP_SERVERNAME "YOUR_APP_SERVERNAME"
#define APP_SERVERPORT  "YOUR_APP_SERVERPORT"
#define APP_ENDPOINT "YOUR_APP_ENDPOINT"

/*
 * Cometa credentials. (for the server application)
 * 
 * COMETA_APP_NAME - cometa registered application name 
 * COMETA_APP_KEY -  cometa registered application key
 */
#define COMETA_APP_NAME "YOUR_COMETA_APP_NAME"
#define COMETA_APP_KEY "YOUR_COMETA_APP_KEY"

/*
 * Device credentials.
 *
 * DEVICE_ID - the ID of this device to use in Cometa
 * DEVICE_KEY - the key of this device for authenticating with the server application
 */
#define DEVICE_ID "YOUR_DEVICE_ID"
#define DEVICE_KEY  "YOUR_DEVICE_KEY"

/* 
 * The server application will be called by the cometa server for authenticating this device at:
 * http://[APP_SERVERNAME:APP_SERVERNAME]/[APP_ENDPOINT]?device_id=[DEVICE_ID]&device_key=[DEVICE_KEY]&app_key=[COMETA_APP_KEY]&challenge=[from_cometa]
 */

/** constants and globals **/
#define REPLY "Pong!"

char rcvBuf[MESSAGE_LEN];
char sendBuf[128];

/** function definitions **/

/*
 * Callback for messages received from the server application (via cometa).
 *
 * @data - message buffer
 * @data_size - message size
 * 
 * The buffer is reused by the cometa client library. Copy the buffer if needed after returning.
 *
 */
static char *
message_handler(int data_size, void *data) {
	/* save the buffer */
	memcpy(rcvBuf, data, data_size);
	
	/* zero-terminate the buffer for printing */
	rcvBuf[data_size] = '\0';
	printf("DEBUG: in message_handler. Received: \r\n%s\r\n", (char *)rcvBuf);
	
	/*
	 * Here is where the received message is interpreted and proper action taken.
	 */
	
	/* send a bogus reply that should be replaced by the proper reply */
	sprintf(sendBuf, "%s", REPLY);
    printf("DEBUG: sending response:\n%s", sendBuf);

	return sendBuf;
}

/*
 * Application entry point.
 */
int 
main(int argc, char *argv[]) {
	struct cometa *cometa;
	cometa_reply ret;
	
	/* initialize this device to use the cometa library */
	ret = cometa_init(DEVICE_ID, "linux_client", DEVICE_KEY);
	if (ret != COMEATAR_OK) {
		fprintf(stderr, "DEBUG: Error in cometa_init: %d. Exiting.\r\n", ret);
		exit(-1);
	}
	
	/* subscribe to cometa */	
	cometa = cometa_subscribe(COMETA_APP_NAME, COMETA_APP_KEY, APP_SERVERNAME, APP_SERVERPORT, "authenticate");
	if (cometa == NULL) {
		fprintf(stderr, "DEBUG: Error in cometa_subscribe. Exiting.\r\n");
		exit(-1);
	}
	
	/* bind the callback for messages received from the application server (via cometa) */
	ret = cometa_bind_cb(cometa, message_handler);
	if (ret != COMEATAR_OK) {
		fprintf(stderr, "DEBUG: Error in cometa_bind_cb: %d. Exiting.\r\n", ret);
		exit(-1);
	}
	
	/* The main() thread is done, this device is subscribed to cometa and is ready to receive
	 * messages handled by the callback. Normally here is where this application's main loop would start.
     * Otherwise, we need to call pthread_exit explicitly to allow the working threads in
	 * the cometa library to continue and for the callback to be executed even after main completes.
	 */
	pthread_exit(NULL);
}