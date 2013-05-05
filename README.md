Cometa
======
###A hosted API for easily and securely enable two-way, real-time interactions to connected devices.

[www.cometa.io](http://www.cometa.io)

Cometa is an edge server that maintains long-lived, bi-directional HTTP(S) connections to remote devices with a simple publish-subscribe interaction pattern. It offers a hosted API at api.cometa.io that is used by both the software in a device and in the application server that intends to communicate with the device.

A device that "subscribe" to a registered Cometa application allows the associated application server to securely send messages "published" to the device's unique ID communication channel. A message received by the device results in a action by the device and in a response message, that is sent back to the Cometa server and relayed to the application server in a synchronous operation. This way Cometa delivers low-latency, one-to-one messages between your application server and enabled devices regardless of NAT and firewalls.

This repository contains the Cometa device client and server libraries, and examples for a number of different embedded systems OS targets. Very little code is needed in the device, and the provided client library makes it easy for an embedded application to use the Cometa API.

*Note: the Cometa server and hosted API service is currently in private beta.*
Synopsis
--------
Build the libcometa library and examples first. On a linux system, build a client program with the compile flags provided by `pkg-config --cflags libcometa`
and build flags based on the output of `pkg-config --libs libcometa`.

	#include <cometa.h>

	/* todo: add simple client code */
	
	do {
	
	} while(1);

More detailed examples are provided for several target devices and OS.

Installation
--------
To build the cometa client library use the command:

	make

On linux hosted environments proceed with:

	sudo make install

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

    GET /subscribe?<device_id>&<device_key>[&<platform]

Parameters:

* device\_id - the unique device ID (max 32 characters)
* device\_key - the device authentication key
* platform - (optional) an information string used to identify the device type (max 64 characters)

Successful response:

	{
		"status": "200",
		"heartbeat": "60"
	}

If the device is successfully subscribed, in the response hash there is a heartbeat frequency in seconds for the device. This is only a hint and devices can decide whether to use it or to ignore it. The HTTP connection is reverted and persisted.

A status other than 200 in the hash, means that the device has not authenticated and the connection closed. 

Subscribing a device to a Cometa hosted application is the authentication process that allow a device to establish a permanent connection with the Cometa server. Authenticating a device involves a 3-way handshaking between the device itself, the application server, and the Cometa server. A device has a limited time to complete the authentication handshake before the Cometa server times out and closes the connection.

The following diagrams illustrates the device authentication process:

![Authentication](http://www.websequencediagrams.com/cgi-bin/cdraw?lz=b3B0IERldmljZSBBdXRoZW50aWNhdGlvbiBTZXF1ZW5jZQpwYXJ0aWNpcGFuACMIAAYNV2ViQXBwABkNQ29tZXRhCgBSBi0-AAkGOiBHRVQgL3N1YnNjcmliZT88YXBwX25hbWU-JjxkAH4FX2lkPgoANwYtPgCBEAY6IENvbm5lYwCBDgVlc3RhYmxpc2hlZCAoImNoYWxsZW5nZSIpAGIJAIEHBjogaHR0cDoveW91cmFwcC9hAIFQCmU_AGELAG8Ja2V5PiYAgQsFAAQGAFIJPgoAgVkGAIEFCkhNQUMoAHIJLCBzZWNyZXQAeAoAgWUIKABHBzoAGhcpAIFYEQCDAAdTAIIZCGQgKCIyMDAiKQplbmQKCg&s=rose)


1. after sending the initial `GET /subscribe` request to the Cometa Server, the device receives a challenge in the response, typically a sequence identifying the connection. The HTTP connection remains open.

2. the device sends the challenge together with the other parameters to the authentication endpoint of the application server. The application server has an opportunity to authenticate the device at this step, for instance checking the DEVICE\_KEY and DEVICE\ID against a database. If the device is authenticated by the application server, an authorization string is calculated by signing the challenge using the APP\_SECRET.

3. the APP\_KEY and the authorization string returned by the application server are sent to the Cometa Server as "chunked-data", using the HTTP connection established in step 1, which completes the authentication process if signature was calculated correctly.

###Publish

	POST /publish?<device_id>&<app_name>&<app_key>&<auth_signature>

Send a message to the <device\_id> subscribed to <app\_name>.

Parameters:

* device\_id - the unique device ID (max 32 characters)
* app\_name - the application name registered with Cometa
* app\_key - the application key
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
	    "ip_address": "54.241.16.45", 
	    "heartbeat": "1367769699", 
	    "info": "linux_client", 
	    "stats": {
	        "connected_at": "1367596124", 
	        "messages": "4007", 
	        "bytes_up": "5003423", 
	        "bytes_down": "20023", 
	        "latency": "32"
	    }
	}

