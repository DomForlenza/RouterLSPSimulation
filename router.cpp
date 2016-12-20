#include "project3.h"
#include <fstream>
#include <iostream>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <sstream>
#include <omp.h>
#include <map>
#include <iomanip>

using std::endl;
using std::string;
using std::to_string;
using std::cerr;
using std::cout;
using std::vector;
using std::map;

/*------Global Vars-------*/
unsigned short udpPort = 0; 
int udpSocket;
int myId;
int currentLSPVersion = 0;
map<int, RouterLinkData> rLinks;
map<int, Message> lspData;
// For a destination (int) find the next hop:
map<int, DstCostNext> shortestPath;
map<int, DstCostNext> LSPBreakdown(string theTable, int nodeId, int myCost);
std::ofstream outputfile;
bool exitUDP = false;
bool receivedMessage = false;
bool receivedLSP = false;


/*-------Functions-------*/
void TCPSendReceiveProcess(unsigned short masterPort);
void UDPSendReceiveProcess();
void SendMessageToDest(Message tmsg);
void SetupUDPNeighborConnections();
void RunLSP();
void RunDijkstras();
int ReceivedSetupMessage(const Message& theMessage, const sockaddr_in& addr);
int ReceivedLSPMessage(const Message& theMessage);
int ReceivedDataMessage(const Message& theMessage);

// Create initial packet to send
Message CreateInitialPacket(int myId, int dst, int cost);

bool rLinksNotConnected() {
	for (auto const& link : rLinks) {
		if (link.second.connected == false) {
			return false;
		}
	}
	return true;
}


// Get current system time (human readable)
string GetCurrentTime(){
	// http://stackoverflow.com/questions/10654258/get-millisecond-part-of-time
	// Copied verbosely from above source
	tm localTime;
	std::stringstream ss;
    std::chrono::system_clock::time_point t = std::chrono::system_clock::now();
    time_t now = std::chrono::system_clock::to_time_t(t);
    localtime_r(&now, &localTime);

    const std::chrono::duration<double> tse = t.time_since_epoch();
    std::chrono::seconds::rep milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(tse).count() % 1000;

    ss << (1900 + localTime.tm_year) << '-'
        << std::setfill('0') << std::setw(2) << (localTime.tm_mon + 1) << '-'
        << std::setfill('0') << std::setw(2) << localTime.tm_mday << ' '
        << std::setfill('0') << std::setw(2) << localTime.tm_hour << ':'
        << std::setfill('0') << std::setw(2) << localTime.tm_min << ':'
        << std::setfill('0') << std::setw(2) << localTime.tm_sec << '.'
        << std::setfill('0') << std::setw(3) << milliseconds
        << ": ";
    return ss.str();
}

// Setup client connection to client
int SetupClientConn(string ip, unsigned short portNum){
	int masterFD;
	int result;
	struct addrinfo hints;
	struct addrinfo *serverinfo;
	struct addrinfo *p;
	memset(&hints, 0, sizeof hints);	// clears memory
	hints.ai_family = AF_UNSPEC;		// type of IP v4 or v6
	hints.ai_socktype = SOCK_STREAM;	// stream socket not datagram
	//hints.ai_flags = AI_PASSIVE; 		// use my current local host IP
	
	if ((result = getaddrinfo(ip.c_str(), std::to_string(portNum).c_str(), &hints, &serverinfo)) != 0) {
		cerr << "getaddrinfo: " << gai_strerror(result) << endl;
		return -1;
	}
	
	for (p = serverinfo; p != NULL; p = p->ai_next) {
		if ((masterFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			cerr << "Client Error: Failed to make socket." << endl;
			continue;
		}

		if (connect(masterFD, p->ai_addr, p->ai_addrlen) == -1) {
			close(masterFD);
			cerr << "Client Error: Failed to bind socket." << endl;
			continue;
		}
		break;
	}
	
	if (p == NULL)  {
		cerr << "Server Error: Failed to bind, reached null at end of struct." << endl;
		exit(1);
	}
	return masterFD;
}


// Setup connection with server (UDP)
int SetupServerConnection(bool UDP){
	int socketFileDescriptor;
	int result;
	int one = 1;
	struct addrinfo *serverinfo;
	struct addrinfo hints;
	struct addrinfo *p;
	memset(&hints, 0, sizeof hints);	// clears memory
	hints.ai_family = AF_UNSPEC;		// type of IP v4 or v6
	hints.ai_socktype = (UDP) ? SOCK_DGRAM : SOCK_STREAM; 	// datagram
	hints.ai_flags = AI_PASSIVE; 		// use my current local host IP
	
	char hostname[1024];
	gethostname(hostname, 1024);
	hostent * ipRecord = gethostbyname(hostname);
	in_addr * ipAddress = (in_addr * )ipRecord->h_addr;
	string finalIPAddress = inet_ntoa(* ipAddress);
	
	if ((result = getaddrinfo("127.0.0.1", 0, &hints, &serverinfo)) != 0) {
		cerr << "getaddrinfo: " << gai_strerror(result) << endl;
		return -1;
	}
	
	for (p = serverinfo; p != NULL; p = p->ai_next) {
		if ((socketFileDescriptor = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			cerr << "Server Error: Failed to make socket." << endl;
			continue;
		}

		if (setsockopt(socketFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) == -1) {
			cerr << "Server Error: Failed to setSocketOptions." << endl;
			exit(1);
		}

		if (bind(socketFileDescriptor, p->ai_addr, p->ai_addrlen) == -1) {
			close(socketFileDescriptor);
			cerr << "Server Error: Failed to bind." << endl;
			continue;
		}
		break;
	}
	
	freeaddrinfo(serverinfo); // Free the serverinfo memory
	
	if (p == NULL)  {
		cerr << "Server Error: Failed to bind, reached null at end of struct." << endl;
		exit(1);
	}
	return socketFileDescriptor;
}


// Router process
void RouterProcess(unsigned short masterPort){
	
	// Step 1: get udp socket setup, retreive port
	udpSocket = SetupServerConnection(true);
	
	// Step 2: parallel threads for UDP/TCP listeners
	omp_set_num_threads(2);
    #pragma omp parallel
	{
		#pragma omp single nowait
		{
			#pragma omp task
				UDPSendReceiveProcess();
			#pragma omp task
				TCPSendReceiveProcess(masterPort);
		}
	}
}


// TCP Send/Recv process
void TCPSendReceiveProcess(unsigned short masterPort) {
	
	sockaddr_in tmp;
	socklen_t len = sizeof(tmp);
	getsockname(udpSocket, (struct sockaddr *) & tmp, &len);
	udpPort = ntohs (tmp.sin_port);
    
	// 1. Setup TCP to manager
	int managerFD = SetupClientConn("127.0.0.1", masterPort);
	
	// 2. Send manager my UDP port #
	int success = send(managerFD ,(char *) &udpPort,sizeof(unsigned short),0);
	if (success <= 0 ){ 
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << std::endl;
		return ; // Error sending, or server closed conn
	}
	
	// 3. Get my router id, neighbors + ports for neighbors + cost
	RouterInfo myData;
	success = recv(managerFD , (RouterInfo* ) &myData, sizeof(RouterInfo), MSG_WAITALL) ;
	if (success <= 0 ) {
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving") << std::endl;
		return; // Error sending, or server closed conn
	}
    
	// Open files
	myId = (int) myData.nodeAddressNumber;
	outputfile.open("Router" + 	std::to_string(myData.nodeAddressNumber)+ ".out");
	outputfile << GetCurrentTime() << "UDP socket ready at: " << inet_ntoa(tmp.sin_addr) << " port " << udpPort << endl;
	outputfile << GetCurrentTime() << "Manager assigned id received (myID = " << myId << ")" << endl;
	if (myData.connectivityStringLength > 0)
		outputfile << GetCurrentTime() << "Received neighbor list" << endl;
	
	// 3a. Parse receive data, setup vector of neighbors
	string connectivity(myData.connectivityTable,myData.connectivityStringLength);
	string rl;
	std::stringstream ss(connectivity);
	while (std::getline(ss,rl,';')) {
		RouterLinkData dt;
		dt.linkNumber = std::stoi(rl.substr(0, rl.find(",")));
		rl = rl.substr(rl.find(",")+1);
		dt.portNumber = std::stoi(rl.substr(0, rl.find(",")));
		rl = rl.substr(rl.find(",")+1);
		dt.linkCost = std::stoi(rl.substr(0, rl.find(";")));
		dt.connected=false;
		rLinks.insert( std::pair<int, RouterLinkData>(dt.linkNumber, dt));
		outputfile<< GetCurrentTime() << "Neighbor#-> "<< dt.linkNumber << ", port-> " << dt.portNumber << ", cost->" << dt.linkCost << endl;
	}
	
	// 4. Tell manager we are ready to connect to neighbors
	bool go = true;
	success = send(managerFD ,(char *) &go,sizeof(bool),0);
	if (success <= 0 ) {
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << endl;
		return ; // Error sending, or server closed conn
	}
	outputfile << GetCurrentTime() << "Sent \"READY to initialize UDP connections\" to manager." << endl;
	
	// 5. Receive "CONNECT" message:
	go = false;
	while (!go) {
		success = recv(managerFD , (bool*) &go, sizeof(bool), MSG_WAITALL) ;
		if (success <= 0 ) {
			std::cout << ((success == 0) ? "Server Closed Connection on CONN.":"Error Receiving") << endl;
			if (success == 0) return; // Error sending, or server closed conn
		}
	}
	outputfile << GetCurrentTime() << "Received \"Start initializing UDP connections\" from manager." << endl;
    
	// 6. Wait until all UDP neighbor links setup
	SetupUDPNeighborConnections();
	
	// 7. Tell manager that we are ready to run LSP neighbor thingy
	go = true;
	success = send(managerFD ,(char *) &go, sizeof(bool), 0);
	if (success <= 0 ){ 
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << endl;
		if (success == 0 ) return; // Error sending, or server closed conn
	}
	outputfile << GetCurrentTime() << "Sent \"READY to run LSP\" to manager." << endl;
	
	// 8. Wait for manager to tell us run LSP
	go = false;
	while (!go) {
		success = recv(managerFD , (bool*) &go, sizeof(bool), MSG_WAITALL);
		if (success <= 0 ) {
			std::cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving") << endl;
			if (success == 0) return; // Error sending, or server closed conn
		}
	}
	outputfile << GetCurrentTime() << "Received \"Start LSP process\" from manager." << endl;
	
	// 9. Run LSP
	outputfile << GetCurrentTime()<< "Starting LSP  finish." << endl;
	RunLSP();
    
	// 10. Tell manager that LSP is done
	go = true;
	success = send(managerFD ,(char *) &go,sizeof(bool),0);
	if (success <= 0 ) {
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << endl;
		if (success == 0 ) return; // Error sending, or server closed conn
	}
	outputfile << GetCurrentTime() << "Sent \"Finished LSP transmit\" to manager." << endl;
	
	// Wait a bit an see if LSP done.
	receivedLSP = true;
	while (receivedLSP) {
		outputfile << GetCurrentTime() << "Waiting for LSP's to populate network, sleeping for 5 seconds" << endl;
		receivedLSP = false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	}
    
	// Calculate shortest path tree (Dijkstras)
	RunDijkstras();

	
	go = true;
	success = send(managerFD ,(char *) &go,sizeof(bool),0);
	if (success <= 0 ) {
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << endl;
		if (success == 0 ) return; // Error sending, or server closed conn
	}
	outputfile << GetCurrentTime() << "Sent \"READY to route\" to manager." << endl;
	
	// 11. Wait until "EXIT" sent-> send "messages" to indicated neighbors
	bool exit = false;
	while (!exit) {
		Message myData;
		success = recv(managerFD , (Message* ) &myData, sizeof(Message), MSG_WAITALL);
		if (success <= 0 ) {
			outputfile << ((success == 0) ? "Server Closed Connection.":"Error Receiving") << endl;
			if (success == 0) return; // Error sending, or server closed conn
		}
		if (myData.messageformat == 3) {
			outputfile << GetCurrentTime() << "Received order to send message to : " << myData.dst << endl;
			SendMessageToDest(myData);
		} else {
			exit = true;
		}
	}
	exitUDP = exit;
	
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	go = true;
	success = send(managerFD ,(char *) &go, sizeof(bool), 0);
	if (success <= 0 ) {
		std::cout << ((success == 0) ? "Server Closed Connection.":"Error Sending") << std::endl;
		if (success == 0 ) return; // Error sending, or server closed conn
	}
	outputfile << GetCurrentTime() << "Sent \"Exited message\" to manager." << endl;
	close (managerFD);
}

// UDP -> Send/Recv process
void UDPSendReceiveProcess() {
	// Listen for neighbors
	struct sockaddr_in addr;
	socklen_t fromLen = sizeof addr;
	timeval tv;
	tv.tv_sec = 2; // Wait two seconds betweeen receives
	tv.tv_usec = 0;
	if (setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		cout << "Error setting timeout." << endl;
	}
	while (exitUDP == false) {
		Message theMessage;
		memset(&theMessage, 0, sizeof(Message));
		int result = recvfrom(udpSocket, (Message *) &theMessage, sizeof(Message), MSG_WAITALL, (sockaddr*) &addr, &fromLen);
		if (result == 0 ) {
			outputfile << GetCurrentTime() << "Server Closed Connection." << endl; 
			close (udpSocket);
			return; // Error sending, or server closed conn
		}
		if (theMessage.messageformat == 1) {
			if ( ReceivedSetupMessage(theMessage, addr) == 0) {
				return; // Error!
			}
		}
		else if (theMessage.messageformat == 2) {
			// LSP Process
			receivedLSP = true;
			ReceivedLSPMessage(theMessage);
		}
		else if (theMessage.messageformat == 3) {
			if ( ReceivedDataMessage(theMessage) == 0) {
				return;
			}
		}
		else { // Timeout, unrecognized format, etc
            // Skip!
		}
	}
	outputfile << GetCurrentTime() << "Received exit command, closing UDP socket." << endl;
	close(udpSocket);
}

// Receive setup message from manager
int ReceivedSetupMessage(const Message& theMessage, const sockaddr_in& addr) {
	socklen_t fromLen = sizeof addr;
    
	if (theMessage.dst != -1){// Initial conn, send back my ID
        
		// If we don't have the link, create a mapping to the link data
		if (rLinks.count(theMessage.src) == 0){
			outputfile << GetCurrentTime() << "Received initial connection from router->" << theMessage.src << endl;
			RouterLinkData newConnection;
			newConnection.linkCost = theMessage.linkCost;
			newConnection.linkNumber = theMessage.src;
			newConnection.portNumber = theMessage.portNumber;
			newConnection.connToOther = addr;
			newConnection.connected = true;
			rLinks.insert(std::pair<int, RouterLinkData>(theMessage.src, newConnection));
		} else if (rLinks[theMessage.src].connected == false) {
			// If we do have a link already, set connected to true
			// NOTE: this should never happen in our code, but needs to still be sanity checked
			outputfile << GetCurrentTime() << "Received connection from router in my connectivity list->" << theMessage.src << endl;
			rLinks[theMessage.src].connected = true;
		}
		// Send ACK
		Message newMessage = CreateInitialPacket(myId, -1, rLinks[theMessage.src].linkCost);
		int result = sendto(udpSocket,(char *) &newMessage, sizeof(Message), 0, (sockaddr*) &addr, fromLen);
		if (result <= 0 ) {
			outputfile << GetCurrentTime() << ((result == 0) ? "Server Closed Connection.":"Error Receiving conn socket")  << endl; 
			if (result == 0) {
				close (udpSocket);
				return 0; // Error sending, or server closed conn
			}
		}
		outputfile << GetCurrentTime() << "Sent ACK to router->"<<theMessage.src << endl;
	 }
	 else if (theMessage.dst == -1) {
		outputfile << GetCurrentTime() << "Rec ACK from router->"<< theMessage.src << endl;
	 }
	 return 1;
}

// Receive LSP Message
int ReceivedLSPMessage(const Message& theMessage) {
	Message newMessage = theMessage;
	if (lspData.count(theMessage.lspPathSrc) != 0){
		if (lspData[theMessage.lspPathSrc].sequenceNum == theMessage.sequenceNum){
			return 1; // Already sent this to neighbors, do nothing!
		}
	}
    if (myId == theMessage.lspPathSrc) {
		return 1; // Don't store my own info.
    }
    
	// Else need to forward:
	// Store LSP for SRC
	lspData.insert(std::pair<int, Message>(theMessage.lspPathSrc, theMessage));
	outputfile << GetCurrentTime() << "LSP received for router->" << theMessage.lspPathSrc << endl;
	newMessage.src = myId;
	for (auto itr = rLinks.begin(); itr != rLinks.end(); itr++) {
		if ((*itr).second.linkNumber == theMessage.src) continue; // Don't send back along same path
		int result = sendto(udpSocket, (char*) &newMessage, sizeof(Message), MSG_WAITALL, (sockaddr*) &(*itr).second.connToOther, sizeof((*itr).second.connToOther));
        
		if (result <= 0){ // Error!
			outputfile << GetCurrentTime() << "Error forwarding LSP to " << (*itr).second.linkNumber << endl;
		} else { // Conn established
			outputfile << GetCurrentTime() << "LSP forwarded to " << (*itr).second.linkNumber<< " (bytes sent = " << result << ")" << endl;
		}
	}	
	return 1;
}

// Data message received
int ReceivedDataMessage(const Message& theMessage) {
	// Send message to DST OR its me
    // Print to outfile
	if (theMessage.dst == myId) {
		outputfile << GetCurrentTime() << "Received message from " << theMessage.src << endl;
	} else {
		// Socket
		outputfile << GetCurrentTime() << "Received message from " << theMessage.src << " destined for " << theMessage.dst << endl;
		SendMessageToDest(theMessage);
	}
	return 1;
}

// Send message to destination router
void SendMessageToDest(Message tmsg) {
	sockaddr_in other = rLinks[shortestPath[tmsg.dst].next].connToOther;
	socklen_t sizeOther = sizeof other;
	sendto(udpSocket, (char *) &tmsg, sizeof(Message), 0, (sockaddr*) &other, sizeOther);
	outputfile << GetCurrentTime() << "Forwarded message for router " << tmsg.dst << " from " << tmsg.src<< " to next hop router " << shortestPath[tmsg.dst].next << endl;
}


// Setup UDP neighbor connections
void SetupUDPNeighborConnections() {
	if (rLinks.size() > 0)
		outputfile << GetCurrentTime() <<"Setting up connections to neighbors."<<endl;
    
	for (auto itr = rLinks.begin(); itr != rLinks.end(); itr++) {
			if ((*itr).second.connected == false){
                outputfile << GetCurrentTime() << "Attempting to establish connetion with router->"<< (*itr).second.linkNumber << endl; // Send to all neighbors
                
			struct sockaddr_in neighborAddy;
			/* gethostbyname: get the server's DNS entry */
			struct hostent *nEnt;
			nEnt = gethostbyname("127.0.0.1");
			if (nEnt == NULL) {
				cout << "ERROR, no such host as 127.0.01;" << endl;
				exit(0);
			}

			/* Build the server's Internet address */
			bzero((char *) &neighborAddy, sizeof(neighborAddy));
			neighborAddy.sin_family = AF_INET;
			bcopy((char *)nEnt->h_addr, 
			  (char *)&neighborAddy.sin_addr.s_addr, nEnt->h_length);
			neighborAddy.sin_port = htons((*itr).second.portNumber);
			
			Message theMessage = CreateInitialPacket(myId, -2, (*itr).second.linkCost);
			socklen_t neighborAddyLen = sizeof(neighborAddy);
			int result = sendto(udpSocket, (char*) &theMessage, sizeof(Message), MSG_WAITALL, (sockaddr*) &neighborAddy, neighborAddyLen);
			if (result <= 0) { // Error!
				outputfile << GetCurrentTime() << "Error connecting to " << (*itr).second.linkNumber << endl;
				outputfile << GetCurrentTime() << "MESSAGE: " << theMessage.messageformat << " " <<theMessage.src << " " << theMessage.dst << endl;
			} else { // Conn established
				outputfile << GetCurrentTime() << "Connection to " << (*itr).second.linkNumber<< " established (bytes sent = " << result << ")" << endl;
				(*itr).second.connToOther = neighborAddy;
				(*itr).second.connected = true;
			}

		} else {
			outputfile << GetCurrentTime() <<"Connection with router " << (*itr).second.linkNumber << " is already established."<< endl;
		}
	}
	// Wait for all others to respond
	while (!rLinksNotConnected()){
		outputfile << GetCurrentTime() << "Waiting for all connections... sleeping" << endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}


// Create initial packet
Message CreateInitialPacket(int src, int dst, int cost) {
	Message msg;
	msg.messageformat = 1;
	msg.src = src;
	msg.dst = dst;
	msg.linkCost = cost;
	return msg;
}

// Run Link State Protocol
void RunLSP() {
	Message lspMsg;
	lspMsg.messageformat = 2;
	lspMsg.src = myId;
	currentLSPVersion ++;
	lspMsg.sequenceNum = currentLSPVersion;
	lspMsg.lspPathSrc = myId;
	string connTable;
	for (auto const& links : rLinks) {
		connTable += std::to_string(links.second.linkNumber)+ "," + std::to_string(links.second.linkCost) + ","+ std::to_string(links.second.linkNumber) + ";";
	}
	lspMsg.lspPathLength = connTable.size();
	snprintf(lspMsg.lspPathTable,500, "%s",connTable.c_str());
	lspData.insert(std::pair<int, Message>(myId, lspMsg));
	
	for (auto itr = rLinks.begin(); itr != rLinks.end(); itr++) {
		outputfile << GetCurrentTime() << "Sending LSP to " << (*itr).second.linkNumber << endl;
		int result = sendto(udpSocket, (char*) &lspMsg, sizeof(Message), MSG_WAITALL, (sockaddr*) &(*itr).second.connToOther, sizeof((*itr).second.connToOther));
		if (result <= 0) { // Error
			outputfile << GetCurrentTime() << "Error sending LSP to " << (*itr).second.linkNumber << endl;
		} else { // Conn established
			outputfile << GetCurrentTime() << "LSP sent to " << (*itr).second.linkNumber<< " (bytes sent = " << result << ")" << endl;
		}
	}
}

// Run Dijkstras algorithm
void RunDijkstras() {
	outputfile << GetCurrentTime() << "Running Dijkstra's" << endl;
	vector<DstCostNext> lspList;
	map<int, DstCostNext> tentativeList;
	DstCostNext next;
	next.dst = myId;
	next.cost = 0;
	next.next = myId;
	tentativeList.insert(std::pair<int, DstCostNext>(myId, next));
	do {
		// Something in list:
		int lowestCost = -1;
		for (auto const& nxt : tentativeList) {
			if (lowestCost == -1) {
				next = nxt.second;
				lowestCost = next.cost;
			} else if ( next.cost > nxt.second.cost) {
				next = nxt.second;
				lowestCost = next.cost;
			}
		}
		auto itr = tentativeList.find(next.dst);
		tentativeList.erase(itr);
		shortestPath.insert(std::pair<int, DstCostNext>(next.dst, next));
		Message nextLSP = lspData[next.dst];
		auto newNeighbors = LSPBreakdown(string(nextLSP.lspPathTable, nextLSP.lspPathLength), shortestPath[next.dst].next, next.cost);
		for (auto const& nxt : newNeighbors) {
			// Neither in confirmed nor tentative:
			if (shortestPath.count(nxt.first) == 0 && tentativeList.count(nxt.first) == 0) {
				tentativeList.insert(nxt);
			} else if (tentativeList.count(nxt.first) > 0) {
				if (tentativeList[nxt.first].cost > nxt.second.cost) {
					auto itr = tentativeList.find(nxt.first);
					tentativeList.erase(itr);
					tentativeList.insert(std::pair<int, DstCostNext>(nxt.first, nxt.second));
				}
			}
		}

	} while (tentativeList.size() > 0);
	
	outputfile << GetCurrentTime() << "Printing Routing Table: " << endl;
	for (auto const& it : shortestPath){
		outputfile << "    (dest, cost, next): " << it.first << " " << it.second.cost << " " <<it.second.next << endl;
	}
	outputfile << GetCurrentTime() << "Dijkstra's complete." << endl;
}


// LSP Breakdown
map<int, DstCostNext> LSPBreakdown(string theTable,int nodeId, int myCost) {
	std::stringstream ss(theTable);
	string token;
	std::stringstream innerss;
	map<int, DstCostNext> theMap;
	while (std::getline(ss, token, ';')) {
		int id, cost, next;
		id = std::stoi(token.substr(0, token.find(",")));
		cost = std::stoi(token.substr(token.find(",")+1));
		next = std::stoi(token.substr(token.find_last_of(",")+1));
		cost += myCost;
		DstCostNext nodeInfo;
		nodeInfo.dst = id;
		nodeInfo.cost = cost;
		nodeInfo.next = (nodeId == myId) ? next : nodeId;
		theMap.insert(std::pair<int, DstCostNext>(id, nodeInfo));
	}
	return theMap;
}
