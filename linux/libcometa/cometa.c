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
 * @file    cometa.c
 *
 * @brief   Library main code to connect a linux device to the cometa infrastructure.
 * 
 */


#include <string.h>
#ifdef USE_SSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>

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

#include "http_parser.h"
#include "cometa.h"

/** Public structures and constants **/

/* Cometa server FQ names and port */
#define SERVERNAME "ensemble.cometa.io"

#ifdef USE_SSL
#define SERVERPORT  "433"
#else
#define SERVERPORT  "80"
#endif

// used to verify the certificate -- TODO: the certificate should accept *.cometa.io
#define VERIFY_SERVERNAME "service.cometa.io"

/* special one byte chunk-data line from devices */
#define MSG_HEARTBEAT   0x06
/* special one byte chunk-data line from devices */
#define MSG_UPSTREAM    0x07

/* print debugging details on stderr */
#define debug_print(...) \
            do { if (DEBUG) fprintf(stderr, ##__VA_ARGS__); } while (0)

/*
 * The cometa structure contains the connection socket and buffers.
 *
 */
struct cometa {
    int    sockfd;						/* socket to Cometa server */
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
    int flag;                       /* disconnection flag */
#ifdef USE_SSL
    BIO     *bconn;
    SSL     *ssl;
    SSL_CTX *ctx;
#endif    
};

http_parser parser;
http_parser_settings settings;
int header_complete;
int body_complete;
char *body_at;

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

int on_headers_complete(http_parser* _) {
  (void)_;
  printf("\n***HEADERS COMPLETE***\n\n");
  header_complete = 1;
  return 0;
}

int on_body(http_parser* _, const char* at, size_t length) {
  (void)_;
  printf("\n*** BODY ***\n\n");
  printf("Body: %.*s\n", (int)length, at);
  body_complete = 1;
  body_at = (char *)at;
  *(body_at + length) = '\0';
  return 0;
}

int on_message_complete(http_parser* _) {
  (void)_;
  printf("\n***MESSAGE COMPLETE***\n\n");
  return 0;
}

#ifdef USE_SSL
static int 
verify_callback(int ok, X509_STORE_CTX *store)
{
    char data[256];
 
    if (!ok)
    {
        X509 *cert = X509_STORE_CTX_get_current_cert(store);
        int  depth = X509_STORE_CTX_get_error_depth(store);
        int  err = X509_STORE_CTX_get_error(store);
 
        fprintf(stderr, "-Error with certificate at depth: %i\n", depth);
        X509_NAME_oneline(X509_get_issuer_name(cert), data, 256);
        fprintf(stderr, "  issuer   = %s\n", data);
        X509_NAME_oneline(X509_get_subject_name(cert), data, 256);
        fprintf(stderr, "  subject  = %s\n", data);
        fprintf(stderr, "  err %i:%s\n", err, X509_verify_cert_error_string(err));
    }
 
    return ok;
}

static long 
post_connection_check(SSL *ssl, char *host)
{
    X509      *cert;
    X509_NAME *subj;
    char      data[256];
    int       extcount;
    int       ok = 0;
 
    /* Checking the return from SSL_get_peer_certificate here is not strictly
     * necessary.  With our example programs, it is not possible for it to return
     * NULL.  However, it is good form to check the return since it can return NULL
     * if the examples are modified to enable anonymous ciphers or for the server
     * to not require a client certificate.
     */
    if (!(cert = SSL_get_peer_certificate(ssl)) || !host)
        goto err_occured;
    if ((extcount = X509_get_ext_count(cert)) > 0)
    {
        int i;
 
        for (i = 0;  i < extcount;  i++)
        {
            char              *extstr;
            X509_EXTENSION    *ext;
 
            ext = X509_get_ext(cert, i);
            extstr = (char*) OBJ_nid2sn(OBJ_obj2nid(X509_EXTENSION_get_object(ext)));
 
            if (!strcmp(extstr, "subjectAltName"))
            {
                int                  j;
                unsigned char        *data;
                STACK_OF(CONF_VALUE) *val;
                CONF_VALUE           *nval;
                X509V3_EXT_METHOD    *meth;
                void                 *ext_str = NULL;
 
                if (!(meth = X509V3_EXT_get(ext)))
                    break;
                data = ext->value->data;

#if (OPENSSL_VERSION_NUMBER > 0x00907000L)
                if (meth->it)
                  ext_str = ASN1_item_d2i(NULL, &data, ext->value->length,
                                          ASN1_ITEM_ptr(meth->it));
                else
                  ext_str = meth->d2i(NULL, &data, ext->value->length);
#else
                ext_str = meth->d2i(NULL, &data, ext->value->length);
#endif
                val = meth->i2v(meth, ext_str, NULL);
                for (j = 0;  j < sk_CONF_VALUE_num(val);  j++)
                {
                    nval = sk_CONF_VALUE_value(val, j);
                    if (!strcmp(nval->name, "DNS") && !strcmp(nval->value, host))
                    {
                        ok = 1;
                        break;
                    }
                }
            }
            if (ok)
                break;
        }
    }
 
    if (!ok && (subj = X509_get_subject_name(cert)) &&
        X509_NAME_get_text_by_NID(subj, NID_commonName, data, 256) > 0)
    {
        data[255] = 0;
        if (strcasecmp(data, host) != 0)
            goto err_occured;
    }
 
    X509_free(cert);
    return SSL_get_verify_result(ssl);
 
err_occured:
    if (cert)
        X509_free(cert);
    return X509_V_ERR_APPLICATION_VERIFICATION;
}

#define CAFILE "rootcert.pem"
#define CADIR NULL

static SSL_CTX *
setup_client_ctx(void)
{
    SSL_CTX *ctx;
 
    ctx = SSL_CTX_new(SSLv23_method());
    if (SSL_CTX_load_verify_locations(ctx, CAFILE, CADIR) != 1)
        fprintf(stderr, "ERROR: Error loading CA file and/or directory (verify_locations).\n");
    if (SSL_CTX_set_default_verify_paths(ctx) != 1)
        fprintf(stderr, "Error loading default CA file and/or directory (verify_path).\n");

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
    SSL_CTX_set_verify_depth(ctx, 4);
    return ctx;
}
#endif

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
		if ((ret = pthread_rwlock_wrlock(&handle->hlock)) != 0) {
	        fprintf(stderr, "ERROR: in send_heartbeat. Failed to get wrlock. ret = %d. Exiting.\r\n", ret);
	        exit (-1);
	    }
		debug_print("DEBUG: sending heartbeat.\r\n");
		/* send a heartbeat */
		sprintf(handle->sendBuff, "2\n%c\n", MSG_HEARTBEAT);    // "2\n\x06\n"	
#ifdef USE_SSL
    	n = SSL_write(handle->ssl, handle->sendBuff, strlen(handle->sendBuff));
#else
		n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
#endif

	    pthread_rwlock_unlock(&(handle->hlock));
        /* check for SIGPIPE broken pipe */
        if (n <= 0 || handle->flag == 1) { //&& (errno == EPIPE)) {
            /* connection lost */
            debug_print("in send_heartbeat: n = %d, errno = %d\n", (int)n, (int)errno);
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
	int n;
    int ret, ch, len;
	
	handle = (struct cometa *)h;
    /* 
	 * start a forever loop reverting the connection and receiving requests from the server 
	 */
    while (1) {

        /* read the first line of the chuck containing the body length */
        n = 0;
        do {
#ifdef USE_SSL
            ret = SSL_read(handle->ssl, handle->recvBuff + n, 1);
#else
            ret = read(handle->sockfd, handle->recvBuff + n, 1);
#endif
            ch = *(handle->recvBuff + n);
            // printf("--- %d %d\n", n, ch);
            n++;
        } while (ch != 10);
        handle->recvBuff[n] = '\0';
        
        /* convert from hex string to int */
        len = (int)strtol(handle->recvBuff, NULL, 16);
        debug_print("DEBUG: ret = %d - len = %d - n = %d - errno = %d - first line:\r\n%s", ret, len, n, errno, handle->recvBuff);
        /* read the chunk */
        n = 0;
        while (n < len) {
#ifdef  USE_SSL
            ret = SSL_read(handle->ssl, handle->recvBuff + n, len); // sizeof(handle->recvBuff) -  1);
#else
            ret = read(handle->sockfd, handle->recvBuff + n, len);
#endif
            n += ret;
        }
        
        if (n <= 0) {
            debug_print("DEBUG: in message receive loop. Socket read: %d errno: %d.\r\n", n, errno);       
            /* Possibly the server closed the connection. Nothing to recover really. The next heartbeat will attempt a new connection. */
            /* Let the heartbeat thread to attempt a reconnection when the server has closed the socket (keep-alive) */
            handle->flag = 1;
            sleep(1);
            continue;            
        }
 
        /* read a closing new line */
        do {
#ifdef USE_SSL
            ret = SSL_read(handle->ssl, handle->recvBuff + n, 1);
#else
            ret = read(handle->sockfd, handle->recvBuff + n, 1);
#endif
            ch = *(handle->recvBuff + n);
            // printf("... %d %d\n", n, ch);
            n++;    
        } while (ch != 10);
        
        handle->recvBuff[n] = '\0';  
        
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
            handle->flag = 1;
            sleep(1);
            continue;            
        }
        /* check if the connection has been terminated by the server with a SIGPIPE */
        if (n == -1 && errno == EINTR) {
            debug_print("DEBUG: in message receive loop. Socket read: %d errno: %d.\r\n", n, errno);       
            /* Nothing to recover really. The next heartbeat will attempt a new connection. */
            handle->flag = 1;
            sleep(1);
            continue;
        }
        if (n < 0) {
            fprintf(stderr, "ERROR: in message receive loop. Socket read error. nbytes: %d, errno: %d.\r\n", n, errno);
            handle->flag = 1;
            sleep(1);
            continue;
        }

        /* received a command */
        debug_print("DEBUG: received from server:\r\n%s\n", handle->recvBuff);

		/* invoke the user callback */
        if (pthread_rwlock_wrlock(&(handle->hlock)) != 0) {
            fprintf(stderr, "ERROR: in message receive loop. Failed to get wrlock. Exiting.\r\n");
            exit (-1);
        } 
		if (handle->user_cb) {
			response = handle->user_cb(n, handle->recvBuff);
			/* assume to receive a zero-terminated string from the application */
			sprintf(handle->sendBuff, "%x\r\n%s\r\n", (int)strlen(response) + 2, response);
		    debug_print("DEBUG: sending response:\r\n%s\n", handle->sendBuff);
		} else {
			sprintf(handle->sendBuff, "%x\r\n\r\n", 2);
			debug_print("DEBUG: sending empty response.\r\n");
		}
        /* send the response back */
#ifdef USE_SSL
        n = SSL_write(handle->ssl, handle->sendBuff, strlen(handle->sendBuff));
#else
        n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
#endif
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
 * @result the connection socket or -1 - the server name when using SSL
 *
 */

#ifdef USE_SSL
static
char * ensemble_connect(void) 
#else
static
int ensemble_connect(void) 
#endif
	{
    struct addrinfo hints;
	struct addrinfo *result, *rp;
    struct ensemble *sp;
    struct ensemble *sp_min = NULL;
    int n;
    int sockfd;
    long    min = 0x7FFFFFFF;
    struct sockaddr_in *addr;
    char str[INET_ADDRSTRLEN];
	char *ptr;
	    
    /* DNS lookup for Cometa servers in the ensemble */	
	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;     // fill in IP list

	if ((n = getaddrinfo(SERVERNAME, SERVERPORT, &hints, &result)) != 0) {
		fprintf(stderr, "ERROR : getaddrinfo() could not get server name %s resolved (%s).\r\n", SERVERNAME, gai_strerror(n));
#ifdef 	USE_SSL
		return NULL;
#else
	    return -1;
#endif
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

#ifdef USE_SSL
	/* return only the name of the selected server */
	ptr = NULL;
   	if (sp_min != NULL) {
        addr = (struct sockaddr_in *)sp_min->ap->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, str, sizeof str);
		ptr = (char *)malloc(strlen(str) + 1);
		strcpy(ptr, str);  
		fprintf(stderr, "Connecting to server %s (%ld usec)\n", ptr, sp_min->delay);
    }

	return ptr;
#else
 	/* proceed with connecting with the selected server */
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
    
	/* return the socket */
    return sockfd;
#endif

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
cometa_init(const char *device_id,  const char *platform, const char *device_key) {
     
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
        
#ifdef USE_SSL
    if (!SSL_library_init()) {
        fprintf(stderr, "** OpenSSL initialization failed!\n");
        exit(-1);
    }
    SSL_load_error_strings();
    RAND_load_file("/dev/urandom", 1024);
#endif
    	
    /* ignore SIGPIPE and handle socket write errors inline  */
    signal(SIGPIPE, SIG_IGN);

    /* setup the http parser for the responses */
    memset(&settings, 0, sizeof(settings));
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    
    http_parser_init(&parser, HTTP_RESPONSE);
  
	return COMEATAR_OK;
}	/* cometa_init */


/* 
 * Subscribe the initialized device to a registered application. 
 * 
 * @param app_name - the application name
 * @param app_key - the application key
 * @param app_server_name - the application server name
 * @param app_server_port - the application server port
 * @param auth_endpoint - the application server authorization endpoint
 *
 * @info if app_server_name, app_server_port and auth_endpoint are NULL
 * do not perform the server authentication step. Authentication will be
 * only done using the app_key (one-way authentication).
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
	int n, i, ret;
    int ch, len;
    int auth_server;
#ifdef USE_SSL
    long err;
	char server_name[INET_ADDRSTRLEN + 12];
#endif
	
    /* check when called for reconnecting */
    if (conn_save != NULL) {
        /* it is a reconnection */
        conn = conn_save;
        conn->flag = 0;
        /* cancel the receive loop thread */
        pthread_cancel(conn->tloop);
        /* wait for the thread to complete */
        pthread_join(conn->tloop, NULL);
    } else {
        /* allocate data structure when called the first time */
        conn = calloc(1, sizeof(struct cometa));
        conn->flag = 0;
        /* save the global connection pointer for re-connecting */
        conn_save = conn;
    
        /* save the parameters */
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
#ifdef USE_SSL
        conn->ctx = setup_client_ctx();
#endif
        /* initialize the server list */
        TAILQ_INIT(&servers);
    }
    
    /* if all the server parameters are NULL do not perform the server authentication step */
    if (app_server_name == NULL && app_server_port == NULL && auth_endpoint == NULL) {
        auth_server = 0;
    } else {
        auth_server = 1;
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
        if (auth_endpoint)
        	conn->auth_endpoint = strdup(auth_endpoint);
        else {
        	fprintf(stderr, "ERROR : Parameter error (auth_endpoint)\r\n");
        	conn->reply = COMETAR_PAR_ERROR;
            return NULL;		
        }            
    }
    
#ifdef USE_SSL
    /* call ensemble_connect() to get the server name */
	sprintf(server_name, "%s:%s", ensemble_connect(), SERVERPORT);
    conn->bconn = BIO_new_connect(server_name);
    if (!conn->bconn) {
        fprintf(stderr, "Error creating connection BIO.\n");
        conn->reply = COMETAR_ERROR;
      	return NULL;
    }
 
    /* set connection blocking */
    if (BIO_set_nbio(conn->bconn, 0) != 1) {
        fprintf(stderr, "Unable to set BIO to blocking mode.\n");
        conn->reply = COMETAR_ERROR;
        return NULL;     
    }
    
    if (BIO_do_connect(conn->bconn) <= 0) {
        fprintf(stderr, "Error connecting to remote machine.\n");
        conn->reply = COMETAR_ERROR;
	  	return NULL;
    }
     
    conn->ssl = SSL_new(conn->ctx);
    SSL_set_mode(conn->ssl, SSL_MODE_AUTO_RETRY);

    SSL_set_bio(conn->ssl, conn->bconn, conn->bconn);
    
    if (SSL_connect(conn->ssl) <= 0) {
        fprintf(stderr, "Error connecting SSL object.\n");
        conn->reply = COMETAR_ERROR;
      	return NULL;        
    }
	if ((err = post_connection_check(conn->ssl, VERIFY_SERVERNAME)) != X509_V_OK) {
        fprintf(stderr, "-Error: peer certificate: %s\n", X509_verify_cert_error_string(err));
        fprintf(stderr, "Error checking SSL object after connection.\n");
        conn->reply = COMETAR_ERROR;
        return NULL;
    }
    fprintf(stderr, "DEBUG: SSL Connection opened\n");
#else
    /* select and connect to a server from the ensemble */
    conn->sockfd = ensemble_connect();
    if (conn->sockfd == -1) {               /* No address succeeded */
		fprintf(stderr, "ERROR : Could not get server name %s resolved. Is the Cometa server running?\r\n", SERVERNAME);
		conn->reply = COMETAR_ERROR;
	  	return NULL;
	}
#endif

    /*
     * ---------------------- step 1 of cometa authentication: send initial subscribe request to cometa
     *   GET /subscribe?<app_name>&<app_key>&<device_id>[&<platform]
     *
     */
    if (auth_server == 1) {
        if (device.info)
            sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&app_key=%s&device_id=%s&platform=%s HTTP/1.1\r\nHost: api.cometa.io\r\nCometa-Authentication: YES\r\n\r\n\r\n", app_name, app_key, device.id, device.info);
    	else
    		sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&app_key=%s&device_id=%s HTTP/1.1\r\nHost: api.cometa.io\r\nCometa-Authentication: YES\r\n\r\n\r\n", app_name, app_key, device.id);
    } else {
        if (device.info)
            sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&app_key=%s&device_id=%s&platform=%s HTTP/1.1\r\nHost: api.cometa.io\r\nCometa-Authentication: NO\r\n\r\n\r\n", app_name, app_key, device.id, device.info);
    	else
    		sprintf(conn->sendBuff, "GET /subscribe?app_name=%s&app_key=%s&device_id=%s HTTP/1.1\r\nHost: api.cometa.io\r\nCometa-Authentication: NO\r\n\r\n\r\n", app_name, app_key, device.id);
    }
   debug_print("DEBUG: sending URL:\r\n%s", conn->sendBuff);

#ifdef USE_SSL
    n = SSL_write(conn->ssl, conn->sendBuff, strlen(conn->sendBuff));
#else
    n = write(conn->sockfd, conn->sendBuff, strlen(conn->sendBuff));
#endif
    if (n <= 0)  {
        fprintf(stderr, "ERROR: writing to cometa server socket.\r\n");
		conn->reply = COMEATAR_NET_ERROR;
        return NULL;
    }
    /* jump to receiving the authentication confirmation if server authentication is not needed */
    if (auth_server == 0)
        goto end_auth;
        
    /* read response with challenge */
    header_complete = 0;
    body_complete = 0;
    n = 0;
    do {
#ifdef USE_SSL
        ret = SSL_read(conn->ssl, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#else
        ret = read(conn->sockfd, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#endif
        n += ret;
        
        http_parser_execute(&parser, &settings, conn->recvBuff, n);
    } while (!header_complete);
    
    n = 0;
    while (!body_complete) {
#ifdef USE_SSL
        ret = SSL_read(conn->ssl, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#else
        ret = read(conn->sockfd, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#endif
        n += ret;
        
        http_parser_execute(&parser, &settings, conn->recvBuff, n);
    }// while (!body_complete);
    
    if(n < 0) {
        fprintf(stderr, "ERROR: Read error from cometa socket.\r\n");
    }
    
    // strcpy(challenge, conn->recvBuff);
    debug_print("\nDEBUG: received (%zd):\r\n%s", strlen(body_at), body_at);
    strcpy(challenge, body_at);
    goto skip;
    
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
skip:
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
		fprintf(stderr, "ERROR : Application server %s not running. step 2\n", app_server_name);
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
    
    /* check for key mistmach
     *
     * {"response":400,"error":"Application key mismatch."}
     */
     if (strcmp(challenge, "Application key mismatch.") == 0) {
         /* return error */
        debug_print("DEBUG: key mismatch error authenticationg with application server.\r\n");
        conn->reply = COMETAR_AUTH_ERROR;
		return NULL;
     }
     
    /*
     *  ---------------------- step 3 of cometa authentication: send signature back to cometa server
     *
     */
    sprintf(conn->sendBuff, "%x\r\n%s\r\n", (int)strlen(challenge) + 2, challenge);
    debug_print("DEBUG: sending CHUNK to server:\r\n%s", conn->sendBuff);

#ifdef USE_SSL
    n = SSL_write(conn->ssl, conn->sendBuff, strlen(conn->sendBuff));
#else
    n = write(conn->sockfd, conn->sendBuff, strlen(conn->sendBuff));
#endif
    if (n < 0)  {
        fprintf(stderr, "ERROR: writing to cometa socket.\r\n");
		conn->reply = COMEATAR_NET_ERROR;
    	return NULL;
	}
	
end_auth:    
    /* read response with JSON object result */
    n = 0;
    do {
#ifdef USE_SSL
        ret = SSL_read(conn->ssl, conn->recvBuff + n, 1);
#else
        ret = read(conn->sockfd, conn->recvBuff + n, 1);
#endif
        ch = *(conn->recvBuff + n);
        // printf("--- %d %d\n", n, ch);
        n++;    
    } while (ch != 10);
    
    /* read the first line of the chuck containing the body length */
    n = 0;
    do {
#ifdef USE_SSL
        ret = SSL_read(conn->ssl, conn->recvBuff + n, 1);
#else
        ret = read(conn->sockfd, conn->recvBuff + n, 1);
#endif
        ch = *(conn->recvBuff + n);
        n++;
    } while (ch != 10);
    conn->recvBuff[n] = '\0';
    
    /* convert from hex string to int */
    len = (int)strtol(conn->recvBuff, NULL, 16);
    debug_print("DEBUG: first line:\r\n%s", conn->recvBuff);
    
    n = 0;
    while (n < len) {
#ifdef USE_SSL
        ret = SSL_read(conn->ssl, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#else
        ret = read(conn->sockfd, conn->recvBuff + n, sizeof(conn->recvBuff) -  1);
#endif
        n += ret;
    }
    conn->recvBuff[n] = '\0';

    if(n < 0) {
        fprintf(stderr, "ERROR: Read error from cometa socket.\r\n");
    }
    debug_print("DEBUG: received (%zd):\r\n%s\n", strlen(conn->recvBuff), conn->recvBuff);

#ifdef USE_SSL    
    /* read another line */
    do {
        ret = SSL_read(conn->ssl, conn->recvBuff + n, 1);
        ch = *(conn->recvBuff + n);
        n++;    
    } while (ch != 10);
    
    conn->recvBuff[n] = '\0';
    debug_print("DEBUG: received (%zd):\r\n%s\n", strlen(conn->recvBuff), conn->recvBuff);
#endif
    /* 
	 * A JSON object is returned by the Cometa server:
	 * 	 success:{ "status": "200", "heartbeat": "60" } 
	 * 	 failed: { "status": "403" }
	 */
     
    /* simple check if the response contains the 403 status */
	if (strstr(conn->recvBuff, "403")) {
	    debug_print("DEBUG: Error Status 403 returned from Cometa server.\r\n");
		conn->reply = COMETAR_AUTH_ERROR;
		return NULL;
	} 

	/* TODO: extract heartbeat from response */
	conn->hz = 60;	/* default to 1 min */
	
    /* device authentication handshake complete */
    /* ----------------------------------------------------------------------------------------------- */
	
    debug_print("DEBUG: authentication handshake complete.\r\n");
    
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
 * If a Webhook is specified for the Application, the message is relayed by Cometa to the server as specified in the webhook of the app in the registry.
 * If the Application has a storage bucket specified, the message is stored in the data bucket.
 *
 * (MESSAGE_LEN - 12) is the maximum message size.
 *
 */
cometa_reply cometa_send(struct cometa *handle, const char *buf, const int size) {
    int ret;
    ssize_t n;
    
    if (MESSAGE_LEN - 12 < size) {
        /* message too large */
        return COMETAR_PAR_ERROR;
    }
    if ((ret = pthread_rwlock_wrlock(&handle->hlock)) != 0) {
        fprintf(stderr, "ERROR: in send_heartbeat. Failed to get wrlock. ret = %d. Exiting.\r\n", ret);
        exit (-1);
    }
	debug_print("DEBUG: sending message upstream.\r\n");
	
	/* The device uses the MSG_UPSTREAM message marker in the first character to indicate  */
    /* an upstream message that is not a response to a publish request. */
    
    /* send the data-chunk length in hex */
    sprintf(handle->sendBuff, "%x\r\n%c", size + 3, MSG_UPSTREAM);
#ifdef USE_SSL
    n = SSL_write(handle->ssl, handle->sendBuff, strlen(handle->sendBuff));
    /* send the data-chunk which can be binary */
    n = SSL_write(handle->ssl, buf, size);
    /* send a CR-LF */
    n = SSL_write(handle->ssl, "\r\n", 2);
#else
    n = write(handle->sockfd, handle->sendBuff, strlen(handle->sendBuff));
    /* send the data-chunk which can be binary */
    n = write(handle->sockfd, buf, size);
    /* send a CR-LF */
    n = write(handle->sockfd, "\r\n", 2);
#endif
    
    pthread_rwlock_unlock(&(handle->hlock));

    /* check for SIGPIPE broken pipe */
    if ((n < 0) && (errno == EPIPE)) {
        /* connection lost */
        debug_print("in cometa_send: n = %d, errno = %d\n", (int)n, (int)errno);
        /* do nothing and let the heartbeat thread to try to reconnect */
        return COMEATAR_NET_ERROR;
    }
    if (n <= 0) {
        /* connection lost */
        debug_print("in cometa_send: n = %d, errno = %d\n", (int)n, (int)errno);
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
