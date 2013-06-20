/*
 * @file    cometa.c
 *
 * @brief   Library main code to connect a linux device to the cometa infrastructure.
 *
 * Cometa is a cloud infrastructure for embedded systems and connected devices.
 *
 * Copyright (C) 2013, Visible Energy, Inc.
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
#include <sys/queue.h>

#include "cometa.h"

/** Public structures and constants **/

/* Cometa server FQ names and port */
#define SERVERPORT  "7007"
#define SERVERNAME "ensemble.cometa.io"

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

/*
 * Structure used during the connection process to track connections to all
 * the Cometa servers in the ensable.
 */
struct ensemble {
    struct addrinfo *ap;
    long    delay;      /* connection delay */
    int     sockfd;     /* socket used for this server */
    pthread_t tid;      /* thread id */
    TAILQ_ENTRY(ensemble) next;
};

/* ensamble servers list */
TAILQ_HEAD(,ensemble) servers;

/** Library global variables **/

/* global variable holding the device's identity and credentials */
struct {
	char *id;     	/* device id */
	char *key;		/* device key */
	char *info;		/* device platform information */
} device;

/* last used connection */
struct cometa *conn_save = NULL;

/** Functions definitions **/

/*
 * The heartbeat thread.
 *
 * This thread detects a server disconnection and attempts to reconnect to a server in the Cometa ensemble.
 *
 */
static void *
send_heartbeat(void *h) {
	struct cometa *handle, *ret_sub;
    int ret;
    ssize_t n;
	
	handle = (struct cometa *)h;
	usleep(handle->hz * 1000000);
	do {
		if ((ret = pthread_rwlock_rdlock(&handle->hlock)) != 0) {
	        fprintf(stderr, "ERROR: in send_heartbeat. Failed to get wrlock. ret = %d. Exiting.\r\n", ret);
	        exit (-1);
	    }
		debug_print("DEBUG: sending heartbeat.\r\n");
		/* send a heartbeat */
		sprintf(handle->sendBuff, "2\n%c\n", MSG_HEARTBEAT);    // "2\n\x06\n"	
		n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
	    pthread_rwlock_unlock(&(handle->hlock));
        /* check for SIGPIPE broken pipe */
        if ((n < 0) && (errno == EPIPE)) {
            /* connection lost */
            debug_print("in send_heartbeat: n = %d, errno = %d\n", n, (int)errno);
            /* attempt to reconnect */
            /* TODO: add a random delay to avoid server flooding when many devices disconnect at the same time */
            ret_sub = cometa_subscribe(conn_save->app_name, conn_save->app_key, conn_save->app_server_name, conn_save->app_server_port, conn_save->auth_endpoint);
            if (ret_sub == NULL) {
                debug_print("ERROR: attempt to reconnect to the server failed.\n");
            }
        }
		
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
    while (1) {
        n = read(handle->sockfd, handle->recvBuff, sizeof(handle->recvBuff) - 1);
        if ((MESSAGE_LEN - 1) < n) {
            fprintf(stderr, "ERROR: in message receive loop. Message too large. nbytes: %d, errno: %d.\r\n", n, errno);
            continue;
            /* Receiving a large  message works well up to 2 times MESSAGE LEN because sendBuff 
             * is allocated after the recvBuff. 
             * TODO: handle a message larger than 2 x MESSAGE_LEN by reading the first line
             * containing the lenght of the body (in hex).
             */
        }
        /* on STREAMS-based systems read() from a socket returns 0 when the connection is closed */
        if (n == 0) {
            debug_print("DEBUG: in message receive loop. Socket read: %d errno: %d.\r\n", n, errno);       
            /* Nothing to recover really. The next heartbeat will attempt a new connection. */
            /* Let the heartbeat thread to attempt a reconnection when the server has closed the socket (keep-alive) */
            sleep(1);
            continue;            
        }
        /* check if the connection has been terminated by the server with a SIGPIPE */
        if (n == -1 && errno == EINTR) {
            debug_print("DEBUG: in message receive loop. Socket read: %d errno: %d.\r\n", n, errno);       
            /* Nothing to recover really. The next heartbeat will attempt a new connection. */
            sleep(1);
            continue;
        }
        if (n < 0) {
            fprintf(stderr, "ERROR: in message receive loop. Socket read error. nbytes: %d, errno: %d.\r\n", n, errno);
            sleep(1);
            continue;
        }
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
	    } while (handle->recvBuff[p] == 10 || handle->recvBuff[p] == 13); // TODO: check for p > n
        
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
    }
	return NULL;
}	/* recv_loop */

/*
 * Thread to connect to a server of the Cometa ensemble.
 *
 * @params  ptr - a pointer to a struct ensemble
 *
 * @result is NULL.
 *
 */
static
void *server_connect(void *ptr) {
    struct ensemble *sp = (struct ensemble *)ptr;
    struct addrinfo *rp = sp->ap;
    struct timeval  start;  /* connection start time */
    struct timeval  end;    /* connection end time */
    struct timeval delay;
    
    sp->delay = 0;
    /* open a socket */
    sp->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sp->sockfd == -1)
        return NULL;
    /* get start time */
    gettimeofday(&start, NULL);        
    /* connect to the server */
    if (connect(sp->sockfd, rp->ai_addr, rp->ai_addrlen) == -1) {
        sp->sockfd = -1;
        return NULL;
    }
    /* get end time */
    gettimeofday(&end, NULL);
    /* subtract the time */
    timersub(&end, &start, &delay);
    /* save the delay in microseconds */
    sp->delay = delay.tv_sec * 1000000 + delay.tv_usec;
    /* close the socket */
    close(sp->sockfd);
    return NULL;
}   /* server_connect */

/*
 * Connect to the server of the Cometa ensemble with the shortest connection delay.
 *
 * @result the connection socket or -1.
 *
 */
static
int ensemble_connect(void) {
    struct addrinfo hints;
	struct addrinfo *result, *rp;
    struct ensemble *sp;
    struct ensemble *sp_min = NULL;
    int n;
    int sockfd;
    long    min = 0x7FFFFFFF;
    struct sockaddr_in *addr;
    char str[INET_ADDRSTRLEN];
    
    /* DNS lookup for Cometa servers in the ensemble */	
	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;     // fill in IP list

	if ((n = getaddrinfo(SERVERNAME, SERVERPORT, &hints, &result)) != 0) {
		fprintf(stderr, "ERROR : getaddrinfo() could not get server name %s resolved (%s).\r\n", SERVERNAME, gai_strerror(n));
	    return -1;
	}
    
    /* start a thread to connect to each server in the ensemble */
	for (rp = result; rp != NULL; rp = rp->ai_next) { 
        addr = (struct sockaddr_in *)rp->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, str, sizeof str);
        debug_print("DEBUG: ensemble connect. Found IP %s\n", str);
        
        sp = calloc(1, sizeof (struct ensemble));
        /* add the entry to the list */
        TAILQ_INSERT_TAIL(&servers, sp, next);
        /* save the server addrinfo */
        sp->ap = rp;
        /* start a thread to open a connection to the associated server */
        pthread_create(&sp->tid, NULL, server_connect, (void *)sp);
	}
    /* wait for all threads to complete the connection */
    for (sp = TAILQ_FIRST(&servers); sp; sp = sp->next.tqe_next) {
        pthread_join(sp->tid, NULL);
    }
    
    /* find the server with the shortest delay */
    for (sp = TAILQ_FIRST(&servers); sp; sp = sp->next.tqe_next) {
        addr = (struct sockaddr_in *)sp->ap->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, str, sizeof str);
        debug_print("DEBUG: connecting delay for %s: %ld\n", str, sp->delay);
        
        if (sp->sockfd == -1)
            continue;
        if (sp->delay < min) {
            min = sp->delay;
            sp_min = sp;
        }
    }    
 
    sockfd = -1;
    if (sp_min != NULL) {
        addr = (struct sockaddr_in *)sp_min->ap->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, str, sizeof str);
        /* open a socket with the selected server */
        if ((sockfd = socket(sp_min->ap->ai_family, sp_min->ap->ai_socktype, sp_min->ap->ai_protocol)) == -1) {
            fprintf(stderr, "ERROR: Could not open socket to server %s", str);
        }
        else
            fprintf(stderr, "Connecting to server %s (%ld usec)\n", str, sp_min->delay);
        /* connect to server */
        if (connect(sockfd, sp_min->ap->ai_addr, sp_min->ap->ai_addrlen) == -1) {
            fprintf(stderr, "ERROR: Could not connect to server %s", str);
            sockfd = -1;
        }   
    }
    
    freeaddrinfo(result);
    /* free list  */
    while (servers.tqh_first != NULL)
        TAILQ_REMOVE(&servers, servers.tqh_first, next);
    
    return sockfd;
}   /* ensemble_connect */

/*
 * Initialize the application to use the library.  
 *
 * @param device_id	- the id of the device to connect
 * @param device_key - the device key
 * @param platform - an (optional) platform description  
 *
 */
cometa_reply
cometa_init(const char *device_id, const char *device_key, const char *platform) {
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
	
    /* ignore SIGPIPE and handle socket write errors inline  */
    signal(SIGPIPE, SIG_IGN);

	return COMEATAR_OK;
}	/* cometa_init */


/* 
 * Subscribe the initialized device to a registered application. 
 * 
 * @param app_server_name - the application server name
 * @param app_server_port - the application server port
 * @param auth_endpoint - the application server authorization endpoint
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
	
    /* check when called for reconnecting */
    if (conn_save != NULL) {
        /* it is a reconnection */
        conn = conn_save;
        /* cancel the receive loop thread */
        pthread_cancel(conn->tloop);
        /* wait for the thread to complete */
        pthread_join(conn->tloop, NULL);
    } else {
        /* allocate data structure when called the first time */
        conn = calloc(1, sizeof(struct cometa));
        /* save the global connection pointer for re-connecting */
        conn_save = conn;
    
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
        
        /* initialize the server list */
        TAILQ_INIT(&servers);
    }
    
    /* select and connect to a server from the ensemble */
    conn->sockfd = ensemble_connect();
    if (conn->sockfd == -1) {               /* No address succeeded */
		fprintf(stderr, "ERROR : Could not get server name %s resolved. Is the Cometa server running?\r\n", SERVERNAME);
		conn->reply = COMETAR_ERROR;
	  	return NULL;
	}

    /*
     * ---------------------- step 1 of cometa authentication: send initial subscribe request to cometa
     *   GET /subscribe?<app_name>&<device_id>[&<platform]
     *
     */
    if (device.info)
    	sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&device_id=%s&platform=%s HTTP/1.1\r\nHost: api.cometa.io\r\n\r\n\r\n", app_name, device.id, device.info);
	else
		sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&device_id=%s HTTP/1.1\r\nHost: api.cometa.io\r\n\r\n\r\n", app_name, device.id);
    debug_print("DEBUG: sending URL:\r\n%s", conn->sendBuff);
    
    n = write(conn->sockfd, conn->sendBuff, strlen(conn->sendBuff));
    if (n <= 0)  {
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
	hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;     // fill in IP list

	if ((n = getaddrinfo(app_server_name, app_server_port, &hints, &result)) != 0) {
		fprintf(stderr, "ERROR : Could not get server name %s resolved. step 2 (%s)\n", app_server_name, gai_strerror(n));
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
		fprintf(stderr, "ERROR : Application server %s not running. step 2\n", app_server_name, gai_strerror(n));
		conn->reply = COMETAR_ERROR;
	  	return NULL;
	}
	freeaddrinfo(result);           /* No longer needed */

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
	/*  
	 *  {"response":200,"signature":"946604ed1d981eca2879:babc3d687335043f55878b3f1eef94815327d6ad533e7c7f51fb30b8ca4683a1"}
	 */
	
    close(conn->app_sockfd);

	/* TODO: check for response 403 Forbidden device */
    
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
	
    /* initialize and set thread detached attribute */ 
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    /* 
	 * start the receive and heartbeat threads if it is not a reconnection
	 */    
    if ((conn->tloop == 0) && (conn->tbeat == 0))  {
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
    } else {
        /* start a new receive loop thread: needed because it is now a new server and a new socket */
        if (pthread_create(&conn->tloop, &attr, recv_loop, (void *)conn)) {
    		fprintf(stderr, "ERROR: Failed to create main loop thread. Exiting.\r\n");
    		exit(-1);
    	} else
            debug_print("DEBUG: Restarted receive loop.\r");
    }
    pthread_attr_destroy(&attr);
    
	conn->reply = COMEATAR_OK;
	return conn;
}	/* cometa_subscribe */

/*
 * Send a message upstream to the Cometa server. 
 * 
 * The message is relayed by Cometa to another server as specified in the webhook of the app in the registry.
 * MESSAGE_LEN is the maximum message size.
 *
 */
cometa_reply cometa_send(struct cometa *handle, const char *buf, const int size) {
    int ret;
    ssize_t n;
    
    if (MESSAGE_LEN - 12 < size) {
        /* message too large */
        return COMETAR_PAR_ERROR;
    }
    if ((ret = pthread_rwlock_rdlock(&handle->hlock)) != 0) {
        fprintf(stderr, "ERROR: in send_heartbeat. Failed to get wrlock. ret = %d. Exiting.\r\n", ret);
        exit (-1);
    }
	debug_print("DEBUG: sending message upstream.\r\n");
    /* send the data-chunk length in hex */
    sprintf(handle->sendBuff, "%x\r\n", size + 2);
    n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
    /* send the data-chunk which can be binary */
    n = write(handle->sockfd, buf, size);
    /* send a CR-LF */
    n = write(handle->sockfd, "\r\n", 2);
    
    pthread_rwlock_unlock(&(handle->hlock));
    /* check for SIGPIPE broken pipe */
    if ((n < 0) && (errno == EPIPE)) {
        /* connection lost */
        debug_print("in cometa_send: n = %d, errno = %d\n", n, (int)errno);
        /* do nothing and let the heartbeat thread to try to reconnect */
        return COMEATAR_NET_ERROR;
    }
    if (n == 0) {
        /* connection lost */
        debug_print("in cometa_send: n = %d, errno = %d\n", n, (int)errno);
        /* do nothing and let the heartbeat thread to try to reconnect */
        return COMEATAR_NET_ERROR;    
    }

	return COMEATAR_OK;
}   /* cometa_send */

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



