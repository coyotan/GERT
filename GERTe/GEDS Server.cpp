/* 
	This is the primary code for the GEDS server.
	This file implements platform independent definitions for internet sockets.
	Supported platforms are currently Linux(/Unix) and Windows
	To modify the default ports change the numbers in the config.h file.
	
	This code is designed to function with other code from
	https://github.com/GlobalEmpire/GERT/
	
	This code falls under the license located at
	https://github.com/GlobalEmpire/GERT/blob/master/License.md
 */

typedef unsigned char UCHAR; //Creates UCHAR shortcut for Unsigned Character
typedef unsigned short ushort;

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <netinet/ip.h>
#endif

#include <thread> //Include thread type
#include <string> //Include string type
#include <signal.h> //Include signal processing API
#include <iostream>
#include "netty.h"
using namespace std; //Default namespace to std

enum status {
	NORMAL,
	NO_LIBS,
	LIB_LOAD_ERR
};

volatile bool running = true; //SIGINT tracker

void shutdownProceedure(int param) { //SIGINT handler function
	running = false; //Flip tracker to disable threads and trigger main thread's cleanup
};

void loadResolutions() {
	FILE* resolutionFile = fopen("resolutions.geds", "rb");
	while (true) {
		USHORT buf[2];
		GERTaddr addr = {buf[0], buf[1]};
		fread(&buf, 2, 2, resolutionFile);
		char buff[20];
		fread(&buff, 20, 1, resolutionFile);
		GERTkey key(buff);
		cout << "Imported resolution for " << to_string(addr.high) << "-" << to_string(addr.low) << "\n";
		addResolution(addr, key);
		if (feof(resolutionFile) != 0)
			break;
	}
	fclose(resolutionFile);
}

string readableIP(in_addr ip) {
	UCHAR * seg = (UCHAR*)(void*)&ip;
	string high = to_string(seg[0]) + "." + to_string(seg[1]);
	string low = to_string(seg[2]) + "." + to_string(seg[3]);
	return high + "." + low;
}

void loadPeers() {
	FILE* peerFile = fopen("peers.geds", "rb");
	while (true) {
		in_addr ip;
		fread(&ip, 4, 1, peerFile); //Why must I choose between 1, 4 and 4, 1? Or 2, 2?
		portComplex ports;
		fread(&ports, 2, 2, peerFile);
		cout << "Imported peer " << readableIP(ip) << ":" << ports.gate << ":" << ports.peer << "\n";
		addPeer(ip, ports);
		if (feof(peerFile) != 0)
			break;
	}
	fclose(peerFile);
}

int main() {

	int libErr = loadLibs(); //Load gelib files

	switch (libErr) { //Test for errors loading libraries
		case EMPTY:
			return NO_LIBS;
		case UNKNOWN:
			return LIB_LOAD_ERR;
	}

	startup(); //Startup servers

	//System processing
	signal(SIGINT, &shutdownProceedure); //Hook SIGINT with custom handler
	thread processor(process);
	runServer();

	//Shutdown and Cleanup sequence
	processor.join(); //Cleanup processor (wait for it to die)

	shutdown();//Cleanup servers
	
	return NORMAL; //Return with exit state "NORMAL" (0)
}
