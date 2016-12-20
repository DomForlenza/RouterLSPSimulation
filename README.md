# Created by Dominick Forlenza/Roger Marquez/AJ Copely


TO compile manger and router processes:
- Type “make” in the console to compile the manager/router program.


—[manager]—
Implemented in manager.cpp
Contains the logic for the manager process and creates the Manager.out file with manager step information.

Manager functions:
1) Wait initial connection from router processes
2) Send connectivity table and router numbers
3) Store connectivity tables
4) Send source and destinations for messages to routers
5) Send/Receive messages from routers
6) Send message to allow routers to start LSP and check that the routers are ready to forward messages


TO INVOKE manager:
- Type ./manager <input file>


Also included in package:
- router.cpp (Contains logic for routing process and creates the RouterN.out file with router step information for each router created)

Router functions:
1) Start TCP and UDP connections (multithreaded)
2) Send/receive messages from manager
3) Set up connections to neighbor routers
4) Run LSP
5) Run Dijkstras algorithm

- project3.h (Contains struct logic for router and manager)


Message formats:
- Four message structs were used.
	- RouterInfo:
		- (send initial connectivity info: node address number, connectivity string length, connectivity table)
	- Message: 
		- (message format, source, destination, link cost, port number, LSP path source, LSP path length, LSP Path Table, and Sequence Number)
	- RouterLinkData:
		- (link cost, link number, port number, sockaddr_in connection to other, connected, current LSP Information message struct)
	- RouterData: 
		- (file descriptor, router number, port number, router link data table, ready to route messages, router exited)
		

As of 12/5/16 the program runs successfully to completion with the proper output file creation and formatting.


To clean the directory of executables and files:
- Type “make clean”