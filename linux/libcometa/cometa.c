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

/* @file
 * Cometa client library main code for vanilla linux systems.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <signal.h> 
#include <unistd.h>
#include <err.h>
#include <time.h>
#include <pthread.h>

#include "cometa.h"

/** Public structures and constants **/

/* Cometa server FQ name and port */
#define SERVERPORT  "7007"
#define SERVERNAME "api.cometa.io"

/* special one byte chunk-data line from devices */
#define MSG_HEARTBEAT 0x06

/* print debugging details on stderr */
#define debug_print(...) \
            do { if (DEBUG) fprintf(stderr, ##__VA_ARGS__); } while (0)

/*
 * The cometa structure contains the connection socket and buffers.
 *
 */
struct cometa {
	int	sockfd;						/* socket to Cometa server */
	char recvBuff[MESSAGE_LEN];		/* received buffer */
        char sendBuff[MESSAGE_LEN];		/* send buffer */
	int	app_sockfd;					/* socket to the application server */
	char *app_name;					/* application name */
	char *app_key;					/* application key */
	char *app_server_name;			/* application server IP */
	char *app_server_port;			/* application server port */
	char *auth_endpoint;			/* application server authentication endpoint */
	cometa_message_cb user_cb;		/* message callback */
	pthread_t	tloop;				/* thread for the receive loop */
	pthread_t	tbeat;				/* thread for the heartbeat */
	pthread_rwlock_t hlock;     	/* lock for heartbeat */
	int	hz;							/* heartbeat period in sec */	
	cometa_reply reply;				/* last reply code */
};

/** Library global variables **/

/* global variable holding the device's identity and credentials */
struct {
	char *id;     	/* device id */
	char *key;		/* device key */
	char *info;		/* device platform information */
} device;

/** Functions definitions **/

/*
 * The heartbeat thread.
 */
static void *
send_heartbeat(void *h) {
	struct cometa *handle;
	int n;
	
	handle = (struct cometa *)h;
	usleep(handle->hz * 1000000);
	do {
		if (pthread_rwlock_rdlock(&(handle->hlock)) != 0) {
	        fprintf(stderr, "ERROR: in send_heartbeat. Failed to get wrlock. Exiting.\r\n");
	        exit (-1);
	    }
		debug_print("DEBUG: sending heartbeat.\r\n");
		/* send a heartbeat */
		sprintf(handle->sendBuff, "2\n%c\n", MSG_HEARTBEAT);    // "2\n\x06\n"	
		n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
	    pthread_rwlock_unlock(&(handle->hlock));
		
		while (usleep(handle->hz * 1000000) == -1 && (errno == EINTR))
			/* interrupted by a SIGNAL */
			continue;
	} while (1);
}	/* send_heartbeat */

/* 
 * The receive and dispatch loop thread.
 */
static void *
recv_loop(void *h) {
	char *response;
	struct cometa *handle;
	int n, p;
	
	handle = (struct cometa *)h;
    /* 
	 * start a forever loop reverting the connection and receiving requests from the server 
	 */
    while ((n = read(handle->sockfd, handle->recvBuff, sizeof(handle->recvBuff)-1)) > 0) {
        if (pthread_rwlock_rdlock(&(handle->hlock)) != 0) {
            fprintf(stderr, "ERROR: in message receive loop. Failed to get wrlock. Exiting.\r\n");
            exit (-1);
        } 
        handle->recvBuff[n] = 0;
        /* received a command */
        debug_print("DEBUG: received from server:\r\n%s", handle->recvBuff);

		/* the message is an HTTP data-chunk with the first line containing the message length in hex */
		/* skip the first line containing the length of the data chunk */
        p = 0;
        while (handle->recvBuff[p] != 10 && handle->recvBuff[p] != 13)
        	 p++;
        do {
		p++;
	} while (handle->recvBuff[p] == 10 || handle->recvBuff[p] == 13);

		/* invoke the user callback */
		if (handle->user_cb) {
			response = handle->user_cb((n - p), (handle->recvBuff) + p);
			sprintf(handle->sendBuff, "%x\r\n%s\r\n", (int)strlen(response) + 2, response);
		    debug_print("DEBUG: sending response:\r\n%s", handle->sendBuff);
		} else {
			sprintf(handle->sendBuff, "%x\r\n\r\n", 2);
			debug_print("DEBUG: sending empty response.\r\n");
		}
		
        /* send the response back */
        n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
        pthread_rwlock_unlock(&(handle->hlock));

        if (n < 0)  {
            fprintf(stderr, "ERROR: in message receive loop. Failed to write to socket. Exiting.\r\n");
			exit(-1);
        }
    }
	return NULL;
}	/* recv_loop */

/*
 * Initialize the application to use the library.  
 *
 * @param device_id	- the id of the device to connect
 * @param platform - an (optional) platform description 
 * @param device_key - the device key
 *
 */
cometa_reply
cometa_init(const char *device_id, const char *platform, const char *device_key) {
	if (device_id && (strlen(device_id) <= DEVICE_ID_LEN))
		device.id = strdup(device_id);
	else
		return COMETAR_PAR_ERROR;
	if (device_key && (strlen(device_key) <= DEVICE_KEY_LEN)) 
		device.key = strdup(device_key);
	else
		return COMETAR_PAR_ERROR;
	if (platform)
		device.info = strdup(platform);
	else
		device.info = NULL;
	
	return COMEATAR_OK;
}	/* cometa_init */


/* 
 * Subscribe the initialized device to a registered application. 
 * 
 * @param app_server_ip - the application server IP address
 * @param app_name - the application name
 * @param app_key - the application key
 *
 * @return	- the connection handle
 *
 */
 
struct cometa *
cometa_subscribe(const char *app_name, const char *app_key, const char *app_server_name, const char *app_server_port, const char *auth_endpoint) {
	struct cometa *conn;
	struct addrinfo hints;
	struct addrinfo *result, *rp; 
   	int data_p, data_s;
    char challenge[128];
	pthread_attr_t attr;
	int n, i;
#ifdef NODEF	
	struct sockaddr_in serv_addr;
#endif
	
	conn = calloc(1, sizeof(struct cometa));
	
	/* save the parameters */
	if (app_server_name)
		conn->app_server_name = strdup(app_server_name);
	else {
		fprintf(stderr, "ERROR : Parameter error (app_server_name).\r\n");
		conn->reply = COMETAR_PAR_ERROR;
        return NULL;		
	}
	if (app_server_port)
		conn->app_server_port = strdup(app_server_port);
	else {
		fprintf(stderr, "ERROR : Parameter error (app_server_port)\r\n");
		conn->reply = COMETAR_PAR_ERROR;
        return NULL;		
	}	
	if (app_name)
		conn->app_name = strdup(app_name);
	else {
		fprintf(stderr, "ERROR : Parameter error (app_name)\r\n");
		conn->reply = COMETAR_PAR_ERROR;
        return NULL;		
	}
	if (app_key)
		conn->app_key = strdup(app_key);
	else {
		fprintf(stderr, "ERROR : Parameter error (app_key)\r\n");
		conn->reply = COMETAR_PAR_ERROR;
        return NULL;		
	}
	if (auth_endpoint)
		conn->auth_endpoint = strdup(auth_endpoint);
	else {
		fprintf(stderr, "ERROR : Parameter error (auth_endpoint)\r\n");
		conn->reply = COMETAR_PAR_ERROR;
        return NULL;		
	}
	
	/* DNS lookup for Cometa server */	
	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_NUMERICSERV;     // fill in IP list

	if ((n = getaddrinfo(SERVERNAME, SERVERPORT, &hints, &result)) != 0) {
		fprintf(stderr, "ERROR : Could not get server name resolved (%s). Is the Cometa server running?\r\n", gai_strerror(n));
		conn->reply = COMETAR_ERROR;
	    return NULL;
	}	
	for (rp = result; rp != NULL; rp = rp->ai_next) {
	     conn->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	     if (conn->sockfd == -1)
	         continue;

	    if (connect(conn->sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
	         break;                  /* Success */

	    close(conn->sockfd);
	}
	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "ERROR : Could not get server name resolved (%s). Is the Cometa server running?\r\n", gai_strerror(n));
		conn->reply = COMETAR_ERROR;
	  	return NULL;
	}
	freeaddrinfo(result);           /* No longer needed */

#ifdef NODEF
	/* create the connection socket */
	if((conn->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("ERROR : Could not create socket \n");
		conn->reply = COMETAR_ERROR;
        return NULL;
    }
	/* initialize Cometa server address */
    memset(&serv_addr, '0', sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVERPORT); 
    if(inet_pton(AF_INET, SERVERIP, &serv_addr.sin_addr)<=0) {
        printf("ERROR: inet_pton error occured\n");
		conn->reply = COMETAR_ERROR;
		return NULL;
    } 
	/* connect to the Cometa server */
    if(connect(conn->sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
       	printf("ERROR: Connect Failed \n");
		conn->reply = COMETAR_ERROR;
       	return NULL;
    }
#endif

    /*
     * ---------------------- step 1 of cometa authentication: send initial subscribe request to cometa
     *   GET /subscribe?<app_name>&<device_id>[&<platform]
     *
     */
    if (device.info)
    	sprintf(conn->sendBuff, "GET /subscribe?app_name=cometatest&device_id=%s&platform=%s HTTP/1.1\r\nHost: api.cometa.io\r\n\r\n\r\n", device.id, device.info);
	else
		sprintf(conn->sendBuff, "GET /subscribe?app_name=cometatest&device_id=%s HTTP/1.1\r\nHost: api.cometa.io\r\n\r\n\r\n", device.id);
    debug_print("DEBUG: sending URL:\r\n%s", conn->sendBuff);
    
    n = write(conn->sockfd, conn->sendBuff, strlen(conn->sendBuff));
    if (n < 0)  {
        fprintf(stderr, "ERROR: writing to cometa server socket.\r\n");
		conn->reply = COMEATAR_NET_ERROR;
        return NULL;
    }
    /* read response with challenge */
    n = read(conn->sockfd, conn->recvBuff, sizeof(conn->recvBuff) -  1);
    conn->recvBuff[n] = 0; 

    if(n < 0) {
        fprintf(stderr, "ERROR: Read error from cometa socket.\r\n");
    }
    debug_print("DEBUG: received (%zd):\r\n%s", strlen(conn->recvBuff), conn->recvBuff);
    
    data_p = strlen(conn->recvBuff);
    /* copy the last line into challenge buffer */
    while (((conn->recvBuff[data_p] == 10) || (conn->recvBuff[data_p] == 13) || (conn->recvBuff[data_p] == 0)) && (data_p >= 0))
        data_p--;	/* skip last NULL, CR or LF */
    /* get the beggining of the line with the JSON object returned */
    while ((conn->recvBuff[data_p] != 13) && (conn->recvBuff[data_p] != 10) && (data_p >= 0)) {
    	data_p--;	/* get to the beginning of the last line */
    }
    while (((conn->recvBuff[data_p] == 10) || (conn->recvBuff[data_p] == 13)) && (data_p >= 0))
    	data_p++;	/* skip last CR or LF */

    if (data_p < 0) {
        fprintf(stderr, "ERROR: Error in buffer from cometa during authentication.\r\n" );
		conn->reply = COMETAR_AUTH_ERROR;
        return NULL;
    }
    /* copy buffer into challenge with the JSON object */
    i = 0;
    while (conn->recvBuff[data_p] != 10 && conn->recvBuff[data_p] != 13 && data_p < MESSAGE_LEN) {
    	challenge[i++] = conn->recvBuff[data_p++];
    }
    challenge[i] = '\0'; /* overwrite CR or LF */
    debug_print("DEBUG: challenge:\r\n%s\n", challenge);

    /*
     *  ---------------------- step 2 of cometa authentication: authenticate device with application server
     *    GET /authenticate?<device_id>&<device_key>&<app_key>&<challenge>
     *
     */

	/* DNS lookup for application server */	
	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_NUMERICSERV;     // fill in IP list

	if ((n = getaddrinfo(app_server_name, app_server_port, &hints, &result)) != 0) {
		fprintf(stderr, "ERROR : Could not get server name resolved. step 1 (%s)\n", gai_strerror(n));
		conn->reply = COMETAR_ERROR;
	    return NULL;
	}	
		
	for (rp = result; rp != NULL; rp = rp->ai_next) {
	     conn->app_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	     if (conn->app_sockfd == -1)
	         continue;

	    if (connect(conn->app_sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
	         break;                  /* Success */

	    close(conn->app_sockfd);
	}
	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "ERROR : Could not get application server name resolved. (%s)\n", gai_strerror(n));
		conn->reply = COMETAR_ERROR;
	  	return NULL;
	}
	freeaddrinfo(result);           /* No longer needed */

#ifdef NODEF
    if((conn->app_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("ERROR : Could not create socket \n");
		conn->reply = COMETAR_ERROR;
        return NULL;
    } 
	/* application server IP address */
    memset(&serv_addr, '0', sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(app_server_port); 
    if(inet_pton(AF_INET, app_server_ip, &serv_addr.sin_addr)<=0) {
        printf("ERROR: inet_pton error occured\n");
		conn->reply = COMETAR_ERROR;
		return NULL;
	} 
	/* connect to the application server */
    if(connect(conn->app_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
       	printf("ERROR: Connect Failed \n");
		conn->reply = COMETAR_ERROR;
		return NULL;
    } 
#endif

    /* send HTTP GET /authenticate request to app server */
    sprintf(conn->sendBuff,"GET /%s?device_id=%s&device_key=%s&app_key=%s&challenge=%s HTTP/1.1\r\nHost: api.cometa.io\r\n\r\n\r\n",
            conn->auth_endpoint, device.id, device.key, conn->app_key, challenge);
    debug_print("DEBUG: sending URL to app server:\r\n%s", conn->sendBuff);
    
    n = write(conn->app_sockfd, conn->sendBuff, strlen(conn->sendBuff));
    if (n < 0)  {
        fprintf(stderr, "ERROR: writing to application server socket.\r\n");
		conn->reply = COMEATAR_NET_ERROR;
        return NULL;
    }
    
    /* read response with challenge */
    n = read(conn->app_sockfd, conn->recvBuff, sizeof(conn->recvBuff) -  1);
    conn->recvBuff[n] = 0; 

    if(n < 0) {
        fprintf(stderr, "ERROR: Read error from application server socket.\r\n");
    }
    debug_print("DEBUG: received from app server (%zd):\r\n%s\n", strlen(conn->recvBuff), conn->recvBuff);

    close(conn->app_sockfd);
    
    data_p = strlen(conn->recvBuff);
     /* extract signature from last line received in buffer */
    while(conn->recvBuff[data_p] != 34 && data_p >= 0)
         data_p--;    /* find the first quote \" */
    data_p--;	/* skip quote */
    data_s = data_p;
    while(conn->recvBuff[data_s] != 34 && data_s >= 0)
    	 data_s--;
    data_s++;	/* skip quote */
    /* the signature is between data_s and data_p */
    for (i = 0; i < (data_p - data_s + 1); i++)
    	 challenge[i] = conn->recvBuff[i + data_s];
    challenge[i] = '\0';
     
    /*
     *  ---------------------- step 3 of cometa authentication: send signature back to cometa server
     *
     */
    sprintf(conn->sendBuff, "%x\r\n%s\r\n", (int)strlen(challenge) + 2, challenge);
    debug_print("DEBUG: sending URL:\r\n%s", conn->sendBuff);
    
    n = write(conn->sockfd, conn->sendBuff, strlen(conn->sendBuff));
    if (n < 0)  {
        fprintf(stderr, "ERROR: writing to cometa socket.\r\n");
		conn->reply = COMEATAR_NET_ERROR;
    	return NULL;
	}
	
    /* read response with JSON object result */
    n = read(conn->sockfd, conn->recvBuff, sizeof(conn->recvBuff) -  1);
    conn->recvBuff[n] = 0; 

    if(n < 0) {
        fprintf(stderr, "ERROR: Read error from cometa socket.\r\n");
    }
    debug_print("DEBUG: received (%zd):\r\n%s", strlen(conn->recvBuff), conn->recvBuff);
	/* 
	 * A JSON object is returned by the Cometa server:
	 *
	 * 	authentication success:{ "status": "200", "heartbeat": "60" } 
	 *
	 * 	authentication failed: { "status": "403" }
	 */
	
	/* simple check if the response contains the 403 status */
	if (strstr(conn->recvBuff, "403")) {
		conn->reply = COMETAR_AUTH_ERROR;
		return NULL;
	} 
	
	/* TODO: extract heartbeat from response */
	conn->hz = 60;	/* default to 1 min */
	
    /* device authentication handshake complete */
    /* ----------------------------------------------------------------------------------------------- */
	
	/* 
	 * start the receive and heartbeat threads
	 */
	
	/* initialize and set thread detached attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	pthread_rwlock_init(&(conn->hlock),NULL);
	/* start the receive loop */
	if (pthread_create(&conn->tloop, &attr, recv_loop, (void *)conn)) {
		fprintf(stderr, "ERROR: Failed to create main loop thread. Exiting.\r\n");
		exit(-1);
	}
	
	/* start the heartbeat loop */
	if (pthread_create(&conn->tbeat, &attr, send_heartbeat, (void *)conn)) {
		fprintf(stderr, "ERROR: Failed to create heartbeat thread. Exiting.\r\n");
		exit(-1);
	}
	pthread_attr_destroy(&attr);
	
	conn->reply = COMEATAR_OK;
	return conn;
}	/* cometa_subscribe */

/*
 * Bind the @cb callback to the receive loop.
 *
 */
cometa_reply 
cometa_bind_cb(struct cometa *handle, cometa_message_cb cb) {
	handle->user_cb = cb;
	
	return COMEATAR_OK;
}

/*
 * Getter method for the error code.
 */
cometa_reply
cometa_error(struct cometa *handle) {
	return handle->reply;
}



