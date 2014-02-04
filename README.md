Cometa
======
###A hosted API for easily and securely enable two-way, real-time interactions to connected devices.

[www.cometa.io](http://www.cometa.io)

Cometa is an edge server that maintains long-lived, bi-directional HTTP(S) connections to remote devices with a simple publish-subscribe interaction pattern. 
It offers a hosted API at `ensemble.cometa.io` that is used by both firmware in the devices and applications that intend to communicate with them. 
The library in this repository is for use by the firmware in the device. 

After registering with the Cometa service and creating an "application" with the server, a developer will receive an application key and a secret key that
are the credentials used by Cometa to allow communication with all the devices that belong to the "application".

that is used by both the software in a device and in the application server that intends to communicate with the device.

A device that "subscribes" to a registered Cometa "application" is enabled to:

1) receive messages

2) reply to messages 

3) send upstream data (without a request)

Messages are delivered to the devices as raw data, as Cometa does not specify a wire protocol. A message is expected a response from the device and Cometa
will relay that response to the requestor in real-time, without storing any data in the message or in the response.

Upstream data can be either forwarded by a device to a server specified in a Web-hook parameter associated with the registered "application", or stored in the 
Cometa server no-SQL database for a specified amount of time. Data stored this way in the Cometa server are accessible in bulk on a device per device basis, as time series.

A message received by the device results in a action by the device and in a response message, that is sent back to the Cometa server and relayed to the requesting application 
in a synchronous HTTP operation. This way Cometa delivers low-latency, one-to-one messages between an application and enabled devices regardless of NAT and firewalls.

Applications interacting with devices using Cometa can be full fledged server applications that "pubish" messages to the device, or pure client applications using websockets 
for device interaction. Each device "subscribed" to Cometa exposes its own unique endpoint, that is a URI that a client application can use as a websocket for secure,
two-way interactions. From the device standpoint, there is no difference between messages received through a "publish" HTTP POST operation to Cometa, or from a client
application through the associated Cometa websocket.

This repository contains the Cometa device client library, and examples for a number of different embedded systems OS targets.
Very little code is needed in the device, and the provided client library makes it easy for an embedded application to use the Cometa API.

*Note: the Cometa server and hosted API service is currently in private beta.*

Synopsis
--------
Build the libcometa library and examples first. On a linux system, build a client program with the compile flags provided by `pkg-config --cflags libcometa`
and build flags based on the output of `pkg-config --libs libcometa`.

    #include <cometa.h>
    
    #define COMETA_APP_NAME "YOUR_COMETA_APP_NAME"
    #define COMETA_APP_KEY "YOUR_COMETA_APP_KEY"

    #define DEVICE_ID "YOUR_DEVICE_ID"
    #define DEVICE_KEY  "YOUR_DEVICE_KEY"
    
    int 
    main(int argc, char *argv[]) {
        struct cometa *cometa;
        cometa_reply ret;
    	
    	/* Initialize this device to use the cometa library */
    	ret = cometa_init(DEVICE_ID, "linux_client", DEVICE_KEY);
    	if (ret != COMEATAR_OK) {
    		fprintf(stderr, "DEBUG: Error in cometa_init: %d. Exiting.\r\n", ret);
    		exit(-1);
    	}
    	
    	/* Subscribe to cometa using one-step authentication */	
    	cometa = cometa_subscribe(COMETA_APP_NAME, COMETA_APP_KEY, NULL, NULL, NULL);
    	if (cometa == NULL) {
    		fprintf(stderr, "DEBUG: Error in cometa_subscribe. Exiting.\r\n");
    		exit(-1);
    	}
    	
    	/* Bind the callback for messages received from the application server (via cometa) */
    	ret = cometa_bind_cb(cometa, message_handler);
    	if (ret != COMEATAR_OK) {
    		fprintf(stderr, "DEBUG: Error in cometa_bind_cb: %d. Exiting.\r\n", ret);
    		exit(-1);
    	}
    	printf("%s: connection completed for device ID: %s\r\n", argv[0], DEVICE_ID);
    	
        /* 
         * The main() thread is done, this device is subscribed to cometa and is ready to receive
    	 * messages handled by the callback. Here is where this application's main loop starts.
         *
    	 */
        do {
            /* Application main loop */
            
    	    sleep(1);
    	} while(1);
    }

More detailed examples are provided for several target devices and OS.

Installation
--------
To build the cometa client library use the command:

	make

On linux hosted environments proceed with:

	sudo make install
	
If using SSL copy the rootcert.pem CA root certificate in a proper directory for use with SSL_CTX_load_verify_locations().

Usage
------
The first step for a server application to use Cometa is to create an account and to register the application with Cometa. Once the application is registered with a unique name for that account, an application key an application secret are provided.

    Example as provided by the Cometa server upon registration:
      APP_NAME: "testbeta"
      APP_KEY: "1b6630ed1da81ec12878"
      APP_SECRET: "7e127dc4811d51708285"

A device that communicates with the application, must know the same application key, a unique ID and a device key. The device unique ID and device key are generated by the developer/manufacturer and can be arbitrary strings up to 32 bytes of maximum length.

    In the device application:
      DEVICE_ID: "40:6c:8f:08:7d:5c"
      DEVICE_KEY: "054bca5142b26a581b6e"
      APP_KEY: "1b6630ed1da81ec12878"

The interaction with the Cometa server is with a simple publish-subscribe interaction pattern. The devices "subscribe" to the application known to Cometa, and the application server "publish" messages for individual subscribers.

Libraries
----
The application server and devices interact with Cometa with the provided REST API. To make device and server programming easier, several libraries are provided for different platforms and languages. The Cometa client library reference implementation for devices is in C programming language for linux OS targets, and the reference implementation for the application server is in Ruby. A fully functional application server is provided for the Sinatra framework.

REST API Reference
--------

The REST API is the way devices and application servers interact with the Cometa edge server.

The Cometa API is hosted at api.cometa.io and must be accessed via HTTP or HTTPS. Parameters are provided in the query string, with exception of the message in the publish POST method, that is in the POST body instead.

####Response

A response to a API request is a hash containing at least the response code. If the response code is `"response": "200"`, other attributes may be returned in the response hash.

###Subscribe
    
    GET /subscribe?<app_name>&<app_key>&<device_id>[&<platform]
    
    Headers: 
        Cometa-Authentication: [YES | NO]
    
Parameters:

* device\_id - the unique device ID (max 32 characters)
* device\_key - the device authentication key
* app\_key - the application key
* platform - (optional) an information string used to identify the device type (max 64 characters)

Successful response:

	{
		"status": "200",
		"heartbeat": "60",
        "epoch": "1379030944"
	}

If the device is successfully subscribed, in the response hash there is a heartbeat frequency in seconds for the device and the epoch time of the server. 
The heartbeat frequency is only a hint and devices can decide whether to use it or to ignore it. The server epoch time may be useful for embedded device
to initialize a system timer or to synchronize a hardware Real-Time Clock.

Once the device is subscribed, the HTTP(S) connection is reverted and persisted.

A status other than 200 in the hash, means that the device has not authenticated and the connection closed. 

Subscribing a device to a Cometa hosted application is the authentication process that allow a device to establish a permanent connection with the Cometa server. 
Authenticating a device can be done with a one-way authentication or a two-way authentication. The optional `Cometa-Authentication` HTTP header in the request
has the value `YES` if a two-way authentication is needed. Otherwise a one-way authentication is performed. The default value is `YES`.

A one-way authentication is performed by the Cometa server only, using the provided application key. 
A two-way authentication involves a 3-way handshaking between the device itself, the application server, and the Cometa server. 
A device has a limited time to complete the authentication handshake before the Cometa server times out and closes the connection.

The following diagrams illustrates the device two-way authentication process:

![Authentication](http://www.websequencediagrams.com/cgi-bin/cdraw?lz=b3B0IERldmljZSBBdXRoZW50aWNhdGlvbiBTZXF1ZW5jZQpwYXJ0aWNpcGFuACMIAAYNV2ViQXBwABkNQ29tZXRhCgBSBi0-AAkGOiBHRVQgL3N1YnNjcmliZT88YXBwX25hbWU-JjxkAH4FX2lkPgoANwYtPgCBEAY6IENvbm5lYwCBDgVlc3RhYmxpc2hlZCAoImNoYWxsZW5nZSIpAGIJAIEHBjogaHR0cDoveW91cmFwcC9hAIFQCmU_AGELAG8Ja2V5PiYAgQsFAAQGAFIJPgoAgVkGAIEFCkhNQUMoAHIJLCBzZWNyZXQAeAoAgWUIKABHBzoAGhcpAIFYEQCDAAdTAIIZCGQgKCIyMDAiKQplbmQKCg&s=rose)


1. after sending the initial `GET /subscribe` request to the Cometa Server, the device receives a challenge in the response, typically a sequence identifying the connection. The HTTP connection remains open.

2. the device sends the challenge together with the other parameters to the authentication endpoint of the application server. The application server has an opportunity to authenticate the device at this step, for instance checking the DEVICE\_KEY and DEVICE\_ID against a database. If the device is authenticated by the application server, an authorization string is calculated by signing the challenge using the APP\_SECRET.

3. the APP\_KEY and the authorization string returned by the application server are sent to the Cometa Server as "chunked-data", using the HTTP connection established in step 1, which completes the authentication process if signature was calculated correctly.

###Publish

	POST /publish?<device_id>&<app_name>&<app_key>&<auth_timestamp>&<body_MD5>&<auth_signature>

Send a message to the `<device_id>` subscribed to `<app_name>`.

Parameters:

* device\_id - the unique device ID (max 32 characters)
* app\_name - the application name registered with Cometa
* app\_key - the application key
* auth_timestamp - timestamp of the request in Epoch
* body_MD5 - MD5 digest of the message
* auth\_signature - the authorization signature

The POST body contains the message. The message can be any type, including binary, providing that is not larger than 64 KB.

The authorization signature is a HMAC SHA256 hex digest of the URI.

Successful response:

A device may respond with a reply message that is returned back in the "reply" attribute of the response hash.

	{
		"status": "200",
		"device" : "40:6c:8f:08:7d:5c",
		"reply" : "{ "temperature" : "75.3","humidity":"35.7"}"
	}
	
## Generate a Publish signature

The following gist illustrates the Ruby code to generate a Publish signature:

    $ irb
    >> require 'openssl'
    => true
    >> app_name="star"
    => "star"
    >> app_key="a54fca5262b26e58b66e"
    => "a54fca5262b26e58b66e"
    >> app_secret="2367f4aedaf70fd64dd1"
    => "2367f4aedaf70fd64dd1"
    >> device_id="1001-01"
    => "1001-01"
    => auth_timestamp = Time.now.to_i.to_s
    => "1391532237"
    => body_MD5 = Digest::MD5.hexdigest(msg)
    => "3dcd0cd40a25df15c50812c18ddb3398"
    >> cmd = "/publish?device_id=" + device_id + "&app_name=" + app_name + "&app_key=" + app_key + "&auth_timestamp=" + auth_timestamp + "&body_MD5=" + body_MD5
    => "/publish?device_id=1001-01&app_name=star&app_key=a54fca5262b26e58b66e&auth_timestamp=1391532237&body_MD5=3dcd0cd40a25df15c50812c18ddb3398"
    >> auth_signature = OpenSSL::HMAC.hexdigest('sha256', app_secret, cmd)
    => "30c1fc938510b1092a120b494e8e50b9137dc8256ecb535cf09895f51df0cbdf"    
    => cmd += "&auth_signature=" + auth_signature
    => "/publish?device_id=1001-01&app_name=star&app_key=a54fca5262b26e58b66e&auth_timestamp=1391532237&body_MD5=3dcd0cd40a25df15c50812c18ddb3398&auth_signature=30c1fc938510b1092a120b494e8e50b9137dc8256ecb535cf09895f51df0cbdf    
    => "/publish?device_id=1001-01app_name=starapp_key=a54fca5262b26e58b66eauth_timestamp=1391532237body_MD5=3dcd0cd40a25df15c50812c18ddb3398&auth_signature=40d3249144cbfba87cd63c1d51cafe3ce2c8ebfb01a863848dae23f01a97ee68

###Info

	GET /info?<app_key>[&<app\_name>][&<device_id>]&<auth_signature>

Obtain statistics on devices subscribed to an application and on specific devices.

Parameters:

* app\_key - the application key
* device\_id - (optional) the unique device ID (max 32 characters)
* app\_name - (optional) the application name registered with Cometa
* auth\_signature - the authorization signature

Successful response:

	{
	    "status": "200", 
	    "device": "40:6c:8f:08:7d:5c", 
	    "ip_address": "54.241.16.25",
        "heartbeat": "1391533168", 
        "info": "BCMUSI14", 
        "stats": {
            "connected_at": "1391513168", 
            "messages": "11621", 
            "bytes_up": "318478", 
            "bytes_down": "378822", 
            "latency": "126", 
            "websockets": "0"
        }
    }	    

