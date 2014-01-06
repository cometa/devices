/*
 * Cometa is a cloud infrastructure for embedded systems and connected 
 * devices developed by Visible Energy, Inc.
 *
 * Copyright (C) 2013, 2014 Visible Energy, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
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
 * @file    cometa-client.c
 * @brief   Cometa client vanilla linux example.
 *
 * To use in your device, once registered an application with Cometa and obtained an application name, application key and secret, 
 * change the constant literals in "Cometa credentials" and "Device credentials" below.
 * For a two-phase authentication with your own application server and devices, change the constant literals in "Server application details", 
 *
 * Build and install the libcometa library before building this example.
 * Insure the rootcert.pem CA root certificate is installed as well.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>    
#include <sys/socket.h>
#include <net/if.h>

#include <cometa.h>

/*
 * Cometa credentials.
 * 
 * COMETA_APP_NAME - Cometa registered application name 
 * COMETA_APP_KEY -  Cometa registered application key
 */
#define COMETA_APP_NAME "YOUR_COMETA_APP_NAME"
#define COMETA_APP_KEY "YOUR_COMETA_APP_KEY"

/*
 * Device credentials.
 *
 * DEVICE_ID - the ID of this device to use in Cometa as returned by get_device_id()
 * DEVICE_KEY - the key of this device for authenticating with your server application
 */
static char * get_device_id(void);
char *DEVICE_ID = get_device_id();

#define DEVICE_KEY  "YOUR_DEVICE_KEY"

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
 * The server application will be called by the cometa server for authenticating this device at:
 * http://[APP_SERVERNAME:APP_SERVERNAME]/[APP_ENDPOINT]?device_id=[DEVICE_ID]&DEVICE_KEY=[DEVICE_KEY]&app_key=[COMETA_APP_KEY]&challenge=[from_cometa]
 */

/** constants and globals **/
char rcvBuf[MESSAGE_LEN];
char sendBuf[128];

/** functions definitions **/

#define REPLY "Pong!"
/*
 * Callback for messages (requests) received from the application (via cometa).
 *
 * @data_len - length message buffer 
 * @data - message buffer 
 * 
 * The buffer is reused by the cometa client library. Copy the buffer if needed after returning.
 *
 */
static char *
message_handler(const int data_len, void *data) {
	time_t now;
    struct tm  ts;
    char dateBuf[80];

	/* save the buffer */
	memcpy(rcvBuf, data, data_len);
	
	/* time */
	time(&now);
	/* Format time, "ddd yyyy-mm-dd hh:mm:ss zzz" */
	ts = *localtime(&now);
	strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &ts);

	/*
	 * Note: if the message contains binary data do not print.
	 */

	/* zero-terminate the buffer for printing */
	rcvBuf[data_len] = '\0';
	printf("%s: in message_handler.\r\nReceived %d bytes:\r\n%s", dateBuf, data_len, (char *)rcvBuf);
	
	/*
	 * Here is where the received message is interpreted and proper action taken.
	 */
	
	/* DEBUG: send a generic response to the application server */
	sprintf(sendBuf, "%s", REPLY);

	return sendBuf;
}

/*
 *
 * Function called by the main loop to send a timestamp upstream.
 *
 * To show usage of the cometa_send() function to send data upstream.
 *
 */
static void
send_time_upstream(struct cometa *handle) {
    time_t now;
    struct tm  ts;
    char tmbuf[64];
    char dateBuf[80];
    cometa_reply ret;
    
    /* Get current time */
	time(&now);
	
	/* Format time, "ddd yyyy-mm-dd hh:mm:ss zzz" */
	ts = *localtime(&now);
	strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &ts);
	sprintf(sendBuf, "{\"id\":\"%s\",\"time\":\"%s\"}", DEVICE_ID, dateBuf);
	
    fprintf(stderr, "Sending %s (len = %zd)\n", sendBuf, strlen(sendBuf));
    ret = cometa_send(handle, sendBuf, strlen(sendBuf));
    if (ret != COMEATAR_OK) {
        fprintf(stderr, "DEBUG: In send_time_upstream. Error in cometa_send() returned %d\n", ret);
    }
}

/*
 * Use the 3 least significant digits of eth0 MAC address as DEVICE_ID.
 *
 */
static char *
get_device_id(void) {
	int s;
    struct ifreq buffer;
    char *ret = malloc(16);

    s = socket(PF_INET, SOCK_DGRAM, 0);
    memset(&buffer, 0x00, sizeof(buffer));
    strcpy(buffer.ifr_name, "eth0");
    ioctl(s, SIOCGIFHWADDR, &buffer);
    close(s);
    
    sprintf(ret, "%.2X%.2X%.2X", (unsigned char)buffer.ifr_hwaddr.sa_data[3], (unsigned char)buffer.ifr_hwaddr.sa_data[4], (unsigned char)buffer.ifr_hwaddr.sa_data[5]);
	return ret;
}

/*
 * Application entry point.
 *
 */
int 
main(int argc, char *argv[]) {
	struct cometa *cometa;
    pthread_t hbeat_thread;
	cometa_reply ret;

	/* 
     * Initialize this device to use the cometa library.
     *
     * Note: the Cometa library sets to ignore SIGPIPE signals (broken pipe).
     *
     */
	ret = cometa_init(DEVICE_ID, "linux_client", DEVICE_KEY);
	if (ret != COMEATAR_OK) {
		fprintf(stderr, "DEBUG: Error in cometa_init: %d. Exiting.\r\n", ret);
		exit(-1);
	}
    
    /* 
     * Ignore exit status of child processes and avoid zombie processes. 
     *
     */
    signal(SIGCHLD, SIG_IGN);
	
	/* 
     * Subscribe to cometa. 
     */	
    //  use of one-way athentication (by setting  APP_SERVERNAME, APP_SERVERPORT, APP_ENDPOINT to NULL)
    //	cometa = cometa_subscribe(COMETA_APP_NAME, COMETA_APP_KEY, APP_SERVERNAME, APP_SERVERPORT, APP_ENDPOINT);
      cometa = cometa_subscribe(COMETA_APP_NAME, COMETA_APP_KEY, NULL, NULL, NULL);
	if (cometa == NULL) {
		fprintf(stderr, "DEBUG: Error in cometa_subscribe. Exiting.\r\n");
		exit(-1);
	}
	
	/* 
     * Bind the callback for messages received from the application server (via Cometa).
     */
	ret = cometa_bind_cb(cometa, message_handler);
	if (ret != COMEATAR_OK) {
		fprintf(stderr, "DEBUG: Error in cometa_bind_cb: %d. Exiting.\r\n", ret);
		exit(-1);
	}
	printf("%s: connection completed for device ID: %s\r\n", argv[0], DEVICE_ID);
    
	/* 
     * The main() thread is done, this device is subscribed to cometa and is ready to receive
	 * messages handled by the callback. Normally here is where this application's main loop would start.
     * Otherwise, we need to call pthread_exit() explicitly to allow the working threads in
	 * the cometa library to continue and for the callback to be executed even after main completes.
     *
	 */
#ifdef NODEF
	pthread_exit(NULL);
#endif
     
	/*
	 * Main loop
	 */
	do {
		sleep(15);
        /* send a simple message upstream */
        send_time_upstream(cometa);
	} while(1);   
	
}   /* main */
