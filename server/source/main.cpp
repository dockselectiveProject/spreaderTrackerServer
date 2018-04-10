// ConsoleApplication1.cpp : Defines the entry point for the console application.
//

#include "iostream"
#include "ws2tcpip.h"
#include <stdlib.h> 
#include <vector>
#include <thread>
#include <memory>

#pragma comment (lib, "Ws2_32.lib")

#define socketReadSize 2048//defines the maximum amount of data receved from the client
#define diagnosticCycleSampleSet 1000000//number of cycles recorded by the data thread for recording diagnostic information such as the stress on the server
#define port 11111


bool close = false;

struct ThreadBuffers{
	bool request;
	uint32_t instruction;
	void * dataLocation;//will be casted to any type that is required
};

std::string rtrim(std::string in)
{
	in.erase(in.find_last_not_of(" \n\r\t") + 1);
	return in;
}

void launchClientHandler(ThreadBuffers **data, SOCKET clientSocket, bool *setup) {
	std::cout << "thread started" << std::endl;

	ThreadBuffers dataSheet;
	dataSheet.request = false;
	*data = &dataSheet;
	*setup = true;

	char buff[socketReadSize+1];//1 extra null byte to not throw error during string comparisons
	buff[socketReadSize] = 0x00;
	while (!close) {
		memset(buff, 0, socketReadSize);
		int bytesrecvd = recv(clientSocket, buff, socketReadSize, 0);
		
		if (bytesrecvd == SOCKET_ERROR || bytesrecvd == 0) {
			std::cout << "client disconnected: " << bytesrecvd << std::endl;
			break;
		}
		else if (rtrim(buff) == "close") {
			close = true;
			std::cout << "client requested close" << std::endl;
		}
		else {
			//strcpy_s(dataSheet.requestbuffer+1, 255, buff);
			dataSheet.instruction = 0x01;
			dataSheet.dataLocation = reinterpret_cast<int*>(buff);
			dataSheet.request = true;
			send(clientSocket, buff, bytesrecvd + 1, 0);
			while (dataSheet.request) {}
			//std::cout << buff << std::endl;
		}
	}
	dataSheet.instruction = 0xff;
	std::cout << "skdhakdjshakdjas" << std::endl;
	dataSheet.request = true;
	while (dataSheet.request) {}
	std::cout << "skdhakdjshakdjas" << std::endl;

	closesocket(clientSocket);
}

void dataHandler(ThreadBuffers *server) {
	std::cout << "data thread started" << std::endl;
	std::vector<ThreadBuffers*> clients;
	while (!close) {
		if (server->request) {
			if (server->instruction==0x41) {
				clients.push_back (reinterpret_cast<ThreadBuffers*>(server->dataLocation));
			}
			server->request = false;
		}
		for (std::vector<ThreadBuffers*>::iterator i = clients.begin(); i != clients.end(); ++i) {
			if ((*i)->request) {
				if ((*i)->instruction == 0x01) {

					char *dataArray = reinterpret_cast<char*>((*i)->dataLocation);
					std::cout << dataArray << std::flush;
				}
				else if ((*i)->instruction == 0xff) {
					(*i)->request = false;
					clients.erase(i);
					std::cout << "removing client" << std::endl;
					break;
				}
			(*i)->request = false;
			}
		}
	}
}

void listenThread(ThreadBuffers **listenBufferslocation, bool *isSetup){

	ThreadBuffers listenBuffers;
	*listenBufferslocation = &listenBuffers;
	listenBuffers.request = 0;
	*isSetup = true;
	std::vector<std::thread> handlerThreads = std::vector<std::thread>();
	WSADATA wsData;//init window sockets
	WORD ver = MAKEWORD(2, 2);
	int wsOK = WSAStartup(ver, &wsData);
	if (wsOK != 0) {
		std::cerr << "error " << wsOK << " initializing winsock Exiting :(" << std::endl;
		return;
	}

	SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);//init socket
	if (listener == INVALID_SOCKET) {
		std::cerr << "error " << wsOK << " initializing winsock Exiting :(" << std::endl;
		return;
	}
	sockaddr_in sockData;
	sockData.sin_family = AF_INET;
	sockData.sin_port = htons(port);
	sockData.sin_addr.S_un.S_addr = INADDR_ANY;

	bind(listener, (sockaddr*)&sockData, sizeof(sockData));//set socket to listen for connections
	listen(listener, SOMAXCONN);

	while (!close) {
		sockaddr_in client;
		int clientSize = sizeof(client);
		SOCKET clientSocket = accept(listener, (sockaddr*)&client, &clientSize);
		/*
		char host[NI_MAXHOST];
		char service[NI_MAXSERV];
		memset(host, 0, NI_MAXHOST);
		memset(service, 0, NI_MAXSERV);

		if (getnameinfo((sockaddr*)&client, clientSize, host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			std::cout << host << " connected on port" << service << std::endl;
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			std::cout << host << " connected on port " << ntohs(client.sin_port) << std::endl;
		}
		*/
		bool clientReady = false;
		ThreadBuffers *clientDataPointer;
		handlerThreads.push_back(std::thread{ launchClientHandler, &clientDataPointer, clientSocket, &clientReady});
		while (!clientReady) {
		}

		listenBuffers.instruction = 0x41;

		ThreadBuffers** parseLocation = reinterpret_cast<ThreadBuffers**>(&listenBuffers.dataLocation);//create a pointer to the "dataLocation" expecting a ThreadBuffers object
		*parseLocation = clientDataPointer;

		listenBuffers.request = true;
		std::cout << clientDataPointer << std::endl;

	}
	closesocket(listener);//kill the listening socket

	for (size_t i = 0; i < handlerThreads.size();i++) {
		handlerThreads[i].join();
		std::cout << "joined thread" << std::endl;
	}
	//dataThread.join();

	WSACleanup();

    return;
}

int main()
{
	std::cout << "Hello World!" << std::endl;

	bool isSocketThreadSetup = false;
	ThreadBuffers *listenBuffers;
	std::thread dataThread{ listenThread, &listenBuffers, &isSocketThreadSetup };
	while (!isSocketThreadSetup) {}
	dataHandler(listenBuffers);
	dataThread.join();

}

