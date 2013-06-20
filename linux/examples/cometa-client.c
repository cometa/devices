/*
 * Cometa is a cloud infrastructure for embedded systems and connected 
 * devices developed by Visible Energy, Inc.
 *
 * Copyright (C) 2013, Visible Energy, Inc.
 * 
 */

/*
 *
 * @file    cometa-client.c
 * @brief   Cometa client vanilla linux example.
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
#include <signal.h>
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
#ifdef NODEF
#define APP_SERVERNAME "YOUR_APP_SERVERNAME"
#define APP_SERVERPORT  "YOUR_APP_SERVERPORT"
#define APP_ENDPOINT "YOUR_APP_ENDPOINT"
#endif

#define APP_SERVERNAME "ec2-54-241-16-55.us-west-1.compute.amazonaws.com"   // cloudfridge.io
#define APP_SERVERPORT  "7017"  // cf_appserver-cometatest.rb
#define APP_ENDPOINT "/authenticate"
/*
 * Cometa credentials. (for the server application)
 * 
 * COMETA_APP_NAME - cometa registered application name 
 * COMETA_APP_KEY -  cometa registered application key
 */
#ifdef NODEF
#define COMETA_APP_NAME "YOUR_COMETA_APP_NAME"
#define COMETA_APP_KEY "YOUR_COMETA_APP_KEY"
#endif

#define COMETA_APP_NAME "cometatest"
#define COMETA_APP_KEY "946604ed1d981eca2879"

/*
 * Device credentials.
 *
 * DEVICE_ID - the ID of this device to use in Cometa
 * DEVICE_KEY - the key of this device for authenticating with the server application
 */
#ifdef NODEF
#define DEVICE_ID "YOUR_DEVICE_ID"
#define DEVICE_KEY  "YOUR_DEVICE_KEY"
#endif

#define DEVICE_ID "10001"
#define DEVICE_KEY  "777"

/* 
 * The server application will be called by the cometa server for authenticating this device at:
 * http://[APP_SERVERNAME:APP_SERVERNAME]/[APP_ENDPOINT]?device_id=[DEVICE_ID]&device_key=[DEVICE_KEY]&app_key=[COMETA_APP_KEY]&challenge=[from_cometa]
 */

/** constants and globals **/
#define REPLY "Pong!"

char rcvBuf[MESSAGE_LEN];
char sendBuf[128];
char dateBuf[80];

/** functions definitions **/

/*
 * Callback for messages received from the server application (via cometa).
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

	/* save the buffer */
	strcpy(rcvBuf, data);
	
	/* time */
	time(&now);
	/* Format time, "ddd yyyy-mm-dd hh:mm:ss zzz" */
	ts = *localtime(&now);
	strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &ts);

	/*
	 * Note: if the message may contain binary data do not print 
	 */

	/* zero-terminate the buffer for printing */
	rcvBuf[data_len] = '\0';
	printf("%s: in message_handler.\r\nReceived %d bytes:\r\n%s", dateBuf, data_len, (char *)rcvBuf);
	
	/*
	 * Here is where the received message is interpreted and proper action taken.
	 */
	
	/* send a bogus response to the application server */
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
    cometa_reply ret;
    
    /* time */
	time(&now);
	/* Format time, "ddd yyyy-mm-dd hh:mm:ss zzz" */
	ts = *localtime(&now);
	strftime(dateBuf, sizeof(dateBuf), "{\"id\":\"10001\",\"time\":\"%Y-%m-%d %H:%M:%S\"}", &ts);
    fprintf(stderr, "DEBUG: In send_time_upstream. Sending %s (%d)\n", dateBuf, strlen(dateBuf));
    ret = cometa_send(handle, dateBuf, strlen(dateBuf));
    if (ret != COMEATAR_OK) {
        fprintf(stderr, "DEBUG: In send_time_upstream. Error in cometa_send() returned %d\n", ret);
    }
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
	ret = cometa_init(DEVICE_ID, DEVICE_KEY, "linux_client");
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
	cometa = cometa_subscribe(COMETA_APP_NAME, COMETA_APP_KEY, APP_SERVERNAME, APP_SERVERPORT, APP_ENDPOINT);
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
     /*
	 pthread_exit(NULL);
     */
     
	/*
	 * Main loop
	 */
	do {
		sleep(15);
        /* send a simple message upstream */
        send_time_upstream(cometa);
	} while(1);    
}   /* main */
