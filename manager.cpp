#include "project3.h"
#include "router.cpp"
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
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <cstring>
#include <sstream>


using std::endl;
using std::string;
using std::cerr;
using std::cout;
using std::vector;
using std::getline;


/*------Global Vars--------*/
vector<RouterData> routerLinks;
int largestFd = 0;
std::ofstream managerOut;
unsigned short masterPort;

/*------Functions----------*/
void waitingInitialConnection(size_t numRouters, int sfd);
void SendConnectivityTableAndRouterNumber();
void StoreConnectivityTable(std::ifstream& inFile);
void SendMessage(char* msg, size_t sizeOfMsg, int fd);
void SendTargetedSrcToDstMessages(std::ifstream& inFile);
void SendConnectToNeighbors();
void SendStartLSP();
bool CheckRoutersReadyToForward();
void RecMessage(int fd);


// Main
int main(int argc, char* argv []){
	if (argc != 2){
		cerr << "Wrong amount of arguments" << endl;
		return 1;
	}
	
	std::ifstream managerFile(argv[1]);
	if (managerFile) {
		managerOut.open("Manager.out");
		cout <<GetCurrentTime()<< "Starting process" << endl;
		managerOut <<GetCurrentTime()<< "Starting process" << endl;
        
		// Setup TCP socket
		int sfd =  SetupServerConnection(false);
		sockaddr_in tmp;
		socklen_t len = sizeof(tmp);
		getsockname(sfd, (struct sockaddr *) & tmp, &len);
		masterPort = ntohs (tmp.sin_port);
        
		// Listen/accept
		if (listen(sfd, 10) == -1) {
			cerr << "Server Error: Listen failed." << endl;
			exit(1);
		}
		
		// Fork router processes
		size_t numRouters = 0;
		string numFiles;
		getline(managerFile, numFiles);
		numRouters = std::stoi(numFiles);
		pid_t procID = 0; 
		size_t counter = 0;
		managerOut << GetCurrentTime() << "Forking " << numRouters << " router processes now... " << endl;
		for (size_t i = 0; i < numRouters; i++) {
			procID = fork();
			counter++;
			if (procID == -1) {
				cerr << "fork failed!\n" << endl;
				return 1;
			}
			if (procID == 0) {
				break;
			}
		}
        
		if (procID > 0) {
			managerOut<< GetCurrentTime() << "TCP socket ready at: " << inet_ntoa(tmp.sin_addr) << " port " << ntohs (tmp.sin_port) << std::endl;
			waitingInitialConnection(numRouters, sfd);
            
			// Connections are setup
			StoreConnectivityTable(managerFile);
			SendConnectivityTableAndRouterNumber();
			SendConnectToNeighbors();
			SendStartLSP();
			SendTargetedSrcToDstMessages(managerFile);
			close(sfd);
			managerOut << GetCurrentTime() << "Process finished" << endl;
			cout << GetCurrentTime() << "Process finished." << endl;
		}
		else {
			RouterProcess(masterPort);
			return 0 ;
		}
	}
	return 0;
}


// Manager waiting for initial connections from routers
void waitingInitialConnection(size_t numRouters, int sfd) {
	int new_fd = 0;
    managerOut << GetCurrentTime() << "Manager waiting for initial router messages." << endl;
    
	while (routerLinks.size() < numRouters ) {
		new_fd = 0;
        
        // Main accept() loop
		while(new_fd == 0) {
            struct sockaddr_storage theirAddress;
			socklen_t socketSize = sizeof theirAddress;
			new_fd = accept(sfd, (struct sockaddr *)&theirAddress, &socketSize);
			if (new_fd == -1) {
				cerr << "Server Error: Accept failed." << endl;
				continue;
			}
		
			unsigned short port;
			int success = recv(new_fd , (unsigned short* ) &port, sizeof(unsigned short), 0);
			if (success < 0 ) {
				cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving") << endl;
				return; // Error sending, or server closed conn
			}
            
			RouterData newRouter;
			newRouter.routerNumber = (int) routerLinks.size();
			newRouter.portNumber  = port;
			newRouter.routerFileDescriptor = new_fd;
			largestFd = (largestFd < new_fd) ? new_fd : largestFd;
			newRouter.readyToRouteMessages = false;
			newRouter.routerExited = false;
 			managerOut << GetCurrentTime() << "Router connected: rn->" <<newRouter.routerNumber << ", port->" << newRouter.portNumber <<  endl;
			routerLinks.push_back(newRouter);
		}
	}
}


// Store router connectivity table
void StoreConnectivityTable(std::ifstream& inFile){
	string line = "";
	getline(inFile, line);
	while (line.find("-1") == string::npos) {
		std::stringstream ss(line);
		int x;
		RouterLinkData routerLinkInfo;
		ss >> x >> routerLinkInfo.linkNumber >> routerLinkInfo.linkCost;
		routerLinks[x].table.push_back(routerLinkInfo);
		getline(inFile, line);
	}
}

// Send router their connectivity table
void SendConnectivityTableAndRouterNumber(){
	for (size_t i = 0; i < routerLinks.size(); i++) {
		string routerConnectionData;
		RouterInfo toSend;
		toSend.nodeAddressNumber = routerLinks[i].routerNumber;
        
		for (size_t j = 0; j < routerLinks[i].table.size(); j++){
			auto  ln = routerLinks[i].table[j].linkNumber;
			routerConnectionData+=std::to_string(ln) + ",";
			routerConnectionData+=std::to_string(routerLinks[ln].portNumber) + ",";
			routerConnectionData+=std::to_string(routerLinks[i].table[j].linkCost) + ";";
		}
        
		toSend.connectivityStringLength = (unsigned short) routerConnectionData.size();
		snprintf(toSend.connectivityTable,500, "%s",routerConnectionData.c_str());

		int fd = routerLinks[i].routerFileDescriptor;
		SendMessage((char *) &toSend, sizeof(RouterInfo), fd);
		managerOut << GetCurrentTime() << "Sent node id, neighbors, cost list to router " << i << endl;
		bool ready;
		int success = recv(fd , (bool* ) &ready, sizeof(bool), 0) ;
		if (success < 0 ){ 
			std::cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving") << std::endl;
			return; // Error sending, or server closed conn
		}
		managerOut << GetCurrentTime() << "Router " << i << " indicated its ready to connect to its neighbors." << endl;
	}
}

// Send router message
void SendMessage(char* msg, size_t sizeOfMsg, int fd) {
	int result = send(fd, msg, sizeOfMsg, 0);
	if (result < 0) {
		cout << "Error sending message.\n" << endl;
	}
	else if (result == 0){
		cout << "other closed connection.\n" << endl;
	}
}


// Send router their neighbor connections
void SendConnectToNeighbors() {
	for (size_t i = 0; i < routerLinks.size(); i++) {
		bool start = true;
		int fd = routerLinks[i].routerFileDescriptor;
		SendMessage((char *) &start, sizeof(bool), fd);
		managerOut << GetCurrentTime() << "Sent \"Connect to neighbors\" message to router " << i << endl;
        
		int success = 0;
		while ( (success = recv(fd, (bool* ) &start, sizeof(bool), MSG_WAITALL) )<= 0 ){ 
			std::cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving \"fin\" conn neighbors") << std::endl;
			if (success == 0)return; // Error sending, or server closed conn
		}
		managerOut << GetCurrentTime() << "Router " << i << " indicated its send its initial connection packet to its neighbors." << endl;
		
	}
	managerOut << GetCurrentTime() << "All routers have indicated that their initial connection packets have been sent." << endl;
}


/*
 * LSP Send Process
 * Tell each router to send their LSP packets, and wait for the messages to propogate.
 */
void SendStartLSP(){
	
	// At this point, all routers have told us they are ready.
	for (size_t i = 0; i < routerLinks.size(); i++) {
		bool start = true;
		int fd = routerLinks[i].routerFileDescriptor;
		SendMessage((char *) &start, sizeof(bool), fd);
		managerOut << GetCurrentTime() << "Sent \"Start LSP to neighbors\" message to router " << i << endl;
		int success = 0;
		while ((success = recv(fd, (bool* ) &start, sizeof(bool), MSG_WAITALL) )<= 0 ) {
			std::cout << ((success == 0) ? "Server Closed Connection.":"Error Receiving \"fin\" conn neighbors") << std::endl;
            if (success == 0) {
                return; // Error sending, or server closed conn
            }
		}
		managerOut << GetCurrentTime() << "Router " << i << " indicated its has sent its LSP packet to its neighbors." << endl;
	}
	
	
	// Select code based off of code from:
	// http://beej.us/guide/bgnet/output/html/multipage/selectman.html
	int n = largestFd+1;
	fd_set routerfds;
	timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	int rv;
	while (CheckRoutersReadyToForward() == false) {

		FD_ZERO(&routerfds);
		for (size_t i = 0; i < routerLinks.size(); i++) {
			FD_SET(routerLinks[i].routerFileDescriptor, &routerfds);
		}
		rv = select(n, &routerfds, NULL, NULL, &tv);
		if (rv == -1) {
			continue;
		}
		bool ready = false;
		for (size_t i = 0; i < routerLinks.size(); i++) {
			if (FD_ISSET(routerLinks[i].routerFileDescriptor, &routerfds)) {
				int success = recv(routerLinks[i].routerFileDescriptor, (bool* ) &ready, sizeof(bool), MSG_WAITALL);
				if (success > 0 && ready == true){
					managerOut << GetCurrentTime() << "Router->" << i << " has indicated ready to route message." << std::endl;
					routerLinks[i].readyToRouteMessages = true;
				}
			}
		}
	}
	managerOut << GetCurrentTime() << "All routers ready to route messages" << std::endl;	
}


// Check if routers are ready to forward messages
bool CheckRoutersReadyToForward(){
	for (size_t i = 0; i < routerLinks.size(); i++) {
		if (routerLinks[i].readyToRouteMessages == false)
			return false;
	}
	return true;
}

// Check if routers have exited
bool CheckRoutersExited(){
	for (size_t i = 0; i < routerLinks.size(); i++) {
		if (routerLinks[i].routerExited == false)
			return false;
	}
	return true;
}


// Send <SRC, DEST> of message to routers
void SendTargetedSrcToDstMessages(std::ifstream& inFile) {
	string line;
	std::getline(inFile, line);
	vector<SrcToDest> data;
	while (line.find("-1") == string::npos) {
		std::stringstream ss(line);
		int src,dst;
		ss >> src >> dst;
		SrcToDest tmp;
		tmp.src = src;
		tmp.dst = dst;
		data.push_back(tmp);
		std::getline(inFile, line);
	}
	Message myData;
	myData.messageformat = 3;
	for (auto const & dt : data) {
		myData.src = dt.src;
		myData.dst = dt.dst;
		SendMessage((char *) &myData, sizeof(Message), routerLinks[myData.src].routerFileDescriptor);
		managerOut << GetCurrentTime() << "Sent a forward message to router " << dt.src << " with destination of router " << dt.dst << endl;
	}

	managerOut << GetCurrentTime() << "Waiting for all messages to finish routing... sleeping for 10 seconds" << endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(10000));
	
	Message endSend;
	endSend.messageformat = 0;
	for (size_t i = 0; i < routerLinks.size(); i++) {
		int fd = routerLinks[i].routerFileDescriptor;
		managerOut << GetCurrentTime() << "Sending \"Exit\" message to router " << i << endl;
		SendMessage((char *) &endSend, sizeof(endSend), fd);
		managerOut << GetCurrentTime() << "Sent \"Exit\" message to router " << i << endl;
	}
	
	
	// Select code bassed off of code from:
	// http://beej.us/guide/bgnet/output/html/multipage/selectman.html
	int n = largestFd+1;
	fd_set routerfds;
	timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	int rv;
	while (CheckRoutersExited() == false) {
		FD_ZERO(&routerfds);
		for (size_t i = 0; i < routerLinks.size(); i++) {
			FD_SET(routerLinks[i].routerFileDescriptor, &routerfds);
		}
		rv = select(n, &routerfds, NULL, NULL, &tv);
		if (rv == -1) {
			continue;
		}
		bool ready = false;
		for (size_t i = 0; i < routerLinks.size(); i++) {
			if (FD_ISSET(routerLinks[i].routerFileDescriptor, &routerfds)) {
				int success = recv(routerLinks[i].routerFileDescriptor, (bool* ) &ready, sizeof(bool), MSG_WAITALL);
				if (success > 0 && ready == true) {
					managerOut << GetCurrentTime() << "Router->" << i << " has indicated exit status." << std::endl;
					routerLinks[i].routerExited = true;
				}
			}
		}
	}
}
