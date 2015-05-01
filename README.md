# MultiSwitch


code to send and receive UDP packets.

This is the workspace for me to code client.c and server.c.
And the final version of code is in /code folder.
Makefile is to complie client.c and server.c
client.c is to send and receive UDP test packages and decide which port to use.
server.c is to receive and send back UDP test packages from client.c

## install

	cd /path/to/project
	mkdir build && cd build
	cmake ..
	make
	sudo make install

## config

Please edit:

	/etc/multiswitch/server.ini  # server only
	/etc/multiswitch/client.ini  # client only

## usage

### for client

	multiswitchclient
	
	
### for server

	multiswitchserver
	
	
	