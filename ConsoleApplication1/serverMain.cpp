﻿// ConsoleApplication1.cpp : Defines the entry point for the console application.
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
		else if (buff[0] == 's' && buff[1] == ' ') {
			dataSheet.instruction = 0x02;
			dataSheet.dataLocation = (buff+2);
			dataSheet.request = true;
			while (dataSheet.request) {}
		}
		else if (buff[0] == 'f' && buff[1] == ' ') {
			dataSheet.instruction = 0x03;
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, buff+2, bytesrecvd + 1, 0);
		}
		else if (buff[0] == 'a' && buff[1] == ' ') {
			dataSheet.instruction = 0x04;
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, buff+2, bytesrecvd + 1, 0);
		}
		else {
			//strcpy_s(dataSheet.requestbuffer+1, 255, buff);
			dataSheet.instruction = 0x01;
			dataSheet.dataLocation = buff;
			dataSheet.request = true;
			send(clientSocket, buff, bytesrecvd + 1, 0);
			while (dataSheet.request) {}
			//std::cout << buff << std::endl;
		}
	}
	dataSheet.instruction = 0xff;
	dataSheet.request = true;
	while (dataSheet.request) {}

	closesocket(clientSocket);
}

void dataHandler(ThreadBuffers *server, std::vector<std::thread> *pThreadVector) {
	std::cout << "data thread started" << std::endl;
	std::vector<ThreadBuffers*> clients;
	std::vector<std::pair<int, int>> spreaderLocationPairs;
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
				else if ((*i)->instruction==0x02) {
					int n1 = reinterpret_cast<int*>((*i)->dataLocation)[0];
					int n2 = reinterpret_cast<int*>((*i)->dataLocation)[1];
					bool added = false;
					for (std::vector<std::pair<int, int>>::iterator pair = spreaderLocationPairs.begin(); pair != spreaderLocationPairs.end(); ++pair) {
						if (pair->first == n1) {
							pair->second = n2;
							added = true;
							break;
						}
					}
					if (!added) {
						spreaderLocationPairs.push_back(std::pair<int, int>(n1, n2));
					}
					std::cout << "added data pair" << std::endl;
				}
				else if ((*i)->instruction == 0x03) {
					int* n1 = reinterpret_cast<int*>((*i)->dataLocation);
					for (std::vector<std::pair<int, int>>::iterator pair = spreaderLocationPairs.begin(); pair != spreaderLocationPairs.end(); ++pair) {
						if (pair->first == *n1) {
							*n1 = pair->second;
							break;
						}
					}
				}
				else if ((*i)->instruction == 0x04) {
					int* n1 = reinterpret_cast<int*>((*i)->dataLocation);
					for (std::vector<std::pair<int, int>>::iterator pair = spreaderLocationPairs.begin(); pair != spreaderLocationPairs.end(); ++pair) {
						if (pair->second == *n1) {
							*n1 = pair->first;
							break;
						}
					}
				}
				else if ((*i)->instruction == 0xff) {
					int count = i - clients.begin();
					(*i)->request = false;
					clients.erase(i);
					pThreadVector->at(count).join();
					pThreadVector->erase((pThreadVector->begin()+count));
					std::cout << "removing client" << std::endl;
					break;
				}
			(*i)->request = false;
			}
		}
	}
	std::cout << "attempting to join remaining threads" << (pThreadVector)->size() << std::endl;
	for (size_t i = 0; i < (pThreadVector)->size(); i++) {
		if((*pThreadVector)[i].joinable())(*pThreadVector)[i].join();
		std::cout << "joined thread" << std::endl;
	}
}

void listenThread(ThreadBuffers **listenBufferslocation, std::vector<std::thread>** ppHandlerThreads, bool *isSetup){

	ThreadBuffers listenBuffers;
	*listenBufferslocation = &listenBuffers;
	listenBuffers.request = 0;
	std::vector<std::thread> handlerThreads = std::vector<std::thread>();
	*ppHandlerThreads = &handlerThreads;
	*isSetup = true;
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

		if (close) break;

		bool clientReady = false;//initialize client thread
		ThreadBuffers *clientDataPointer;
		handlerThreads.push_back(std::thread{ launchClientHandler, &clientDataPointer, clientSocket, &clientReady});
		while (!clientReady) {
		}
		
		//prepair to send data to the data thread
		listenBuffers.instruction = 0x41;

		ThreadBuffers** parseLocation = reinterpret_cast<ThreadBuffers**>(&listenBuffers.dataLocation);//create a pointer to the "dataLocation" expecting a ThreadBuffers object
		*parseLocation = clientDataPointer;

		listenBuffers.request = true;//submit
		std::cout << clientDataPointer << std::endl;//debug output between the request submition and check to allow it to run in parrallel
		while (listenBuffers.request) {}//wait for the request to be processed

	}
	closesocket(listener);//kill the listening socket

	
	//dataThread.join();

	WSACleanup();

    return;
}

int main()
{
	std::cout << "Hello World!" << std::endl;

	bool isSocketThreadSetup = false;
	ThreadBuffers *listenBuffers;
	std::vector<std::thread> *pHandlerThread;
	std::thread dataThread{ listenThread, &listenBuffers, &pHandlerThread, &isSocketThreadSetup };
	while (!isSocketThreadSetup) {}
	dataHandler(listenBuffers, pHandlerThread);
	dataThread.join();

}


