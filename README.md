Cometa
======
###A hosted API for easily and securely enable two-way, real-time interactions to connected devices.

[www.cometa.io](http://www.cometa.io)

Cometa is an edge server that maintains long-lived, bi-directional HTTP(S) connections to remote devices with a simple publish-subscribe interaction pattern. It offers a hosted API at api.cometa.io that is used by both the client software in a device and in the application server that intends to communicate with the device.

A device that "subscribe" to a registered Cometa application allows the associated application server to securely send messages "published" to the device's unique id communication channel. A message received by the device results in a action by the device and in a response message, that is sent back to the Cometa server and relayed to the application server. This way Cometa delivers low-latency, one-to-one messages between your application server and enabled devices regardless of NAT and firewalls.

This repository contains the Cometa client libraries and examples for a number of different devices and OS targets. The client library makes it easy for an application to use the Cometa API.

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
To build the library use the command:
`make`

On linux hosted environments proceed with:
`make install`
API Documentation
--------
*TODO*