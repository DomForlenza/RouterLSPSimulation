#pragma once
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>

// Router info struct
struct RouterInfo{
	unsigned short nodeAddressNumber;
	unsigned short connectivityStringLength;
	char connectivityTable[500];
};

// SRC -> DEST
struct SrcToDest{
	int src; // if -1 exit.
	int dst;
};

// Message struct
struct Message{
	int messageformat; // 1 = initial conn, 2 = lsp, 3 = send to X message
	int src; // 0(myID) and 2
	int dst; // 0(return == -1) for 2
	int linkCost; // for 1
	unsigned short portNumber; //for 0
	int lspPathSrc;// for 1
	size_t lspPathLength; // for  1
	char lspPathTable[500]; // for 1
	int sequenceNum; 
};

// Router Link Data Struct
struct RouterLinkData{
	int linkCost;
	unsigned short linkNumber;
	unsigned short portNumber;
	sockaddr_in connToOther;
	bool connected;
	Message currentLSPInformation;
};

// Router Data Struct
struct RouterData{
	int routerFileDescriptor;
	unsigned short routerNumber;
	unsigned short portNumber;
	std::vector<RouterLinkData> table;
	bool readyToRouteMessages;
	bool routerExited;
};

// DEST-> COST
struct DstCostNext{
	int dst;
	int cost;
	int next;
};
