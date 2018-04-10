#include "networking.h"

#include "ws2tcpip.h"
#include <list>

//functions in this file aren't thread safe

struct socketopen {
	SOCKET sock;
	bool free;
};

std::list<socketopen> openSockets = std::list<socketopen>();


bool initSockets() {
	WSADATA wsData;//init window sockets
	WORD ver = MAKEWORD(2, 2);
	int wsOK = WSAStartup(ver, &wsData);
	if (wsOK != 0) {
		//std::cerr << "error " << wsOK << " initializing winsock Exiting :(" << std::endl;
		return false;
	}
	return true;
}

int socketCreate() {
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);//init socket
	if (sock == INVALID_SOCKET) {
		//std::cerr << "error " << wsOK << " initializing winsock Exiting :(" << std::endl;
		return -1;
	}
	int counter = 0;
	for (std::list<socketopen>::iterator i = openSockets.begin(); i != openSockets.end(); i++) {
		if (i->free) {
			i->free = false;
			i->sock = sock;
			return counter;
		}
		counter++;
	}
	socketopen so;
	so.sock = sock;
	so.free = false;

	openSockets.push_back(so);
}
