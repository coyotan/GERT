/*
This is the code that provides protocol definition and formatting to the primary server.
This code should be compiled then placed in the "apis" subfolder of the main server.
This code is similarly cross-platform as the main server.
This code DEFINITELY supports Windows and Linux and MIGHT support MacOS (but the main server doesn't.)

This code is designed to function with other code from
https://github.com/GlobalEmpire/GERT/

This code falls under the license located at
https://github.com/GlobalEmpire/GERT/blob/master/License.md
*/

#ifdef _WIN32
#define DLLExport __declspec(dllexport)
#else
#define DLLExport
#endif

#define STARTEXPORT extern "C" {
#define ENDEXPORT }

#include "libHelper.h"
using namespace std;

typedef unsigned char UCHAR;

enum gatewayCommands {
	STATE,
	REGISTER,
	DATA,
	STATUS,
	CLOSE
};

enum gatewayStates {
	FAILURE,
	CONNECTED,
	ASSIGNED,
	CLOSED,
	SENT
};

enum gatewayErrors {
	VERSION,
	BAD_KEY,
	ALREADY_REGISTERED,
	NOT_REGISTERED,
	NO_ROUTE
};

enum gedsCommands {
	REGISTERED,
	UNREGISTERED,
	ROUTE,
	RESOLVE,
	UNRESOLVE,
	LINK,
	UNLINK,
	CLOSEPEER,
	QUERY
};

STARTEXPORT

DLLExport UCHAR major = 1;
DLLExport UCHAR minor = 0;
DLLExport UCHAR patch = 0;

DLLExport void processGateway(Gateway* gate, string packet) {
	if (gate->state == FAILURE) {
		gate->state = CONNECTED;
		sendByGateway(gate, string({ STATE, CONNECTED, (char)major, (char)minor, (char)patch }));
		/*
		 * Response to connection attempt.
		 * CMD STATE (0)
		 * STATE CONNECTED (1)
		 * MAJOR VERSION
		 * MINOR VERSION
		 * PATCH VERSION
		 */
		return;
	}
	UCHAR command = packet.data()[0];
	string rest = packet.erase(0, 1);
	switch (command) {
		case REGISTER: {
			if (gate->state == ASSIGNED) {
				sendByGateway(gate, string({ STATE, FAILURE, ALREADY_REGISTERED }));
				/*
				 * Response to registration attempt when gateway has address assigned.
				 * CMD STATE (0)
				 * STATE FAILURE (0)
				 * REASON ALREADY_REGISTERED (2)
				 */
				return;
			}
			Address request{rest};
			rest.erase(0, 3);
			Key requestkey(rest);
			if (assign(gate, request, requestkey)) {
				sendByGateway(gate, string({ STATE, ASSIGNED }));
				gate->state = REGISTERED;
				/*
				 * Response to successful registration attempt
				 * CMD STATE (0)
				 * STATE REGISTERED (2)
				 */
				string cmd = {REGISTERED};
				cmd += putAddr(request);
				broadcast(cmd);
				/*
				 * Broadcast to all peers registration
				 * CMD REGISTERED (0)
				 * Address (4 bytes)
				 */
			} else
				sendByGateway(gate, string({ STATE, FAILURE, BAD_KEY }));
				/*
				 * Response to failed registration attempt.
				 * CMD STATE (0)
				 * STATE FAILURE (0)
				 * REASON BAD_KEY (1)
				 */
			return;
		}
		case DATA: {
			if (gate->state == CONNECTED) {
				sendByGateway(gate, string({STATE, FAILURE, NOT_REGISTERED}));
				/*
				 * Response to data before registration
				 * CMD STATE (0)
				 * STATE FAILURE (0)
				 * REASON NOT_REGISTERED (3)
				 */
				break;
			}
			GERTc target{rest}; //Assign target address as first 4 bytes
			rest.erase(0, 6);
			string newCmd = string{DATA} + gate->addr.stringify() + rest;
			if (isRemote(target.external) || isLocal(target.external)) { //Target is remote
				sendToGateway(target.external, newCmd); //Send to target
			} else {
				if (queryWeb(target.external)) {
					sendToGateway(target.external, newCmd);
				} else {
					sendByGateway(gate, string({ STATE, FAILURE, NO_ROUTE }));
					/*
					 * Response to failed data send request.
					 * CMD STATE (0)
					 * STATE FAILURE (0)
					 * REASON NO_ROUTE (4)
					 */
					return;
				}
			}
			sendByGateway(gate, string({ STATE, SENT }));
			/*
			 * Response to successful data send request.
			 * CMD STATE (0)
			 * STATE SENT (5)
			 */
			return;
		}
		case STATUS: {
			sendByGateway(gate, string({ STATE, (char)gate->state }));
			/*
			 * Response to state request
			 * CMD STATE (0)
			 * STATE (1 byte, unsigned)
			 * No following data
			 */
			return;
		}
		case CLOSE: {
			sendByGateway(gate, string({ STATE, CLOSED }));
			/*
			 * Response to close request.
			 * CMD STATE (0)
			 * STATE CLOSED (3)
			 */
			if (gate->state == REGISTERED) {
				string cmd = {UNREGISTERED};
				cmd += putAddr(gate->addr);
				broadcast(cmd);
				removeResolution(gate->addr);
				/*
				 * Broadcast that registered gateway left.
				 * CMD UNREGISTERED (1)
				 * Address (4 bytes)
				 */
			}
			closeGateway(gate);
		}
	}
}

DLLExport void processGEDS(peer* geds, string packet) {
	if (geds->state == 0) {
		geds->state = 1;
		sendByPeer(geds, string({ (char)major, (char)minor, (char)patch }));
		/*
		 * Initial packet
		 * MAJOR VERSION
		 * MINOR VERSION
		 * PATCH VERSION
		 */
		return;
	}
	UCHAR command = packet.data()[0];
	string rest = packet.erase(0, 1);
	switch (command) {
		case ROUTE: {
			Address target{rest};
			string cmd = { DATA };
			cmd += rest;
			if (!sendToGateway(target, cmd)) {
				string errCmd = { UNREGISTERED };
				errCmd += putAddr(target);
				sendByPeer(geds, errCmd);
			}
			return;
		}
		case REGISTERED: {
			Address target{rest};
			setRoute(target, geds);
			return;
		}
		case UNREGISTERED: {
			Address target{rest};
			removeRoute(target);
			return;
		}
		case RESOLVE: {
			Address target{rest};
			rest.erase(0, 6);
			Key key = rest;
			addResolution(target, key);
			return;
		}
		case UNRESOLVE: {
			Address target{rest};
			removeResolution(target);
			return;
		}
		case LINK: {
			ipAddr target(rest);
			rest.erase(0, 6);
			portComplex ports = makePorts(rest);
			addPeer(target, ports);
			return;
		}
		case UNLINK: {
			ipAddr target(rest);
			removePeer(target);
			return;
		}
		case CLOSEPEER: {
			sendByPeer(geds, string({ CLOSEPEER }));
			closePeer(geds);
			return;
		}
		case QUERY: {
			Address target{rest};
			if (isLocal(target)) {
				string cmd = { REGISTERED };
				cmd += putAddr(target);
				sendToGateway(target, cmd);
			} else {
				string cmd = { UNREGISTERED };
				cmd += putAddr(target);
				sendByPeer(geds, cmd);
			}
			return;
		}
	}
}

DLLExport void killGateway(Gateway* gate) {
	sendByGateway(gate, string({ CLOSE })); //SEND CLOSE REQUEST
	sendByGateway(gate, string({ STATE, CLOSED })); //SEND STATE UPDATE TO CLOSED (0, 3)
	closeGateway(gate);
}

DLLExport void killGEDS(peer* geds) {
	sendByPeer(geds, string({ CLOSEPEER })); //SEND CLOSE REQUEST
	closePeer(geds);
}

ENDEXPORT

#ifdef _WIN32
bool WINAPI DllMain(HINSTANCE w, DWORD a, LPVOID s) {
	return true;
}
#endif
