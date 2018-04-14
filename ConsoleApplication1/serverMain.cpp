// ConsoleApplication1.cpp : Defines the entry point for the console application.
//

#include "iostream"
#include "ws2tcpip.h"
#include <stdlib.h> 
#include <vector>
#include <thread>
#include <memory>
#include <chrono>


#pragma comment (lib, "Ws2_32.lib")

#define socketReadSize 2048//defines the maximum amount of data receved from the client
#define diagnosticCycleSampleSet 1000000//number of cycles recorded by the data thread for recording diagnostic information such as the stress on the server
#define maxSpreaderNameSize 15//i think they are less than 8
#define maxLocationNameSize 31
#define port 11111


bool close = false;

struct ThreadBuffers{//used for inter thread comunication
	bool request;
	uint32_t instruction;
	void * dataLocation;//will be casted to any type that is required
};

struct Spreader
{//contains info about the spreader
	int id;
	char name[maxSpreaderNameSize + 1];
	time_t lastSeen;
	int locationId;
};
struct Location 
{//contains info about locations
	int id;
	char name[maxLocationNameSize + 1];
	std::vector<int> spreaderIds;
};

std::string rtrim(std::string in)
{
	in.erase(in.find_last_not_of(" \n\r\t") + 1);
	return in;
}

Spreader* findSpreaderID(int id, std::vector<Spreader> * spreaders)
{
	for (size_t counter = 0; counter < spreaders->size(); ++counter) {
		if (spreaders->at(counter).id == id){
			return &spreaders->at(counter);
		}
	}
	return nullptr;

}

void launchClientHandler(ThreadBuffers **data, SOCKET clientSocket, bool *setup)
{
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
		else if (rtrim(buff) == "close") {//close the server(broken atm)
			close = true;
			std::cout << "client requested close" << std::endl;
		}
		else if (buff[0] == 'a' && buff[1] == ' ') {//update spreader location
			dataSheet.instruction = 0x02;           //a <spreaderID><LocationID>
			dataSheet.dataLocation = (buff+2);
			dataSheet.request = true;
			while (dataSheet.request) {}
		}
		else if (buff[0] == 'b' && buff[1] == ' ') {//find location of spreader
			dataSheet.instruction = 0x03;           //b <spreaderID>
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, buff+2, bytesrecvd - 1, 0);
		}
		else if (buff[0] == 'c' && buff[1] == ' ') {//find spreader at location(first in list only need to rethink)
			dataSheet.instruction = 0x04;           //c <locationID>
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, buff+2, bytesrecvd - 1, 0);
		}
		else if (buff[0] == 'd' && buff[1] == ' ') { //add spreader to list of known spreaders
			dataSheet.instruction = 0x05;            //d <spreaderID><spreaderName>
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, "added spreader to list", sizeof("added spreader to list")+1, 0);//should find a better solution to this
		}
		else if (buff[0] == 'e' && buff[1] == ' ') { //add location to list of known spreaders
			dataSheet.instruction = 0x06;            //e <locationID><spreaderName>
			dataSheet.dataLocation = (buff + 2);
			dataSheet.request = true;
			while (dataSheet.request) {}
			send(clientSocket, "added Location to list", sizeof("added Location to list") + 1, 0);//should find a better solution to this
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

void dataHandler(ThreadBuffers *server, std::vector<std::thread> *pThreadVector)
{
	std::cout << "data thread started" << std::endl;

	std::vector<ThreadBuffers*> clients;
	std::vector<std::pair<int, int>> spreaderLocationPairs; //spreader id : location id
	std::vector<Spreader> spreaders;
	std::vector<Location> locations;

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
				else if ((*i)->instruction==0x02) {//update a spreader's location or add the spreader if it has not been located yet
					int spreaderID = reinterpret_cast<int*>((*i)->dataLocation)[0];
					int locationID = reinterpret_cast<int*>((*i)->dataLocation)[1];
					bool added = false;
					for (auto spreaderIterator = spreaders.begin(); spreaderIterator != spreaders.end(); ++spreaderIterator) {
						if (spreaderIterator->id == spreaderID) {//should add a find item with id in vector function
							for (auto locationIterator = locations.begin(); locationIterator != locations.end(); ++locationIterator) {//clear spreader id from current locations cache
								if (locationIterator->id == spreaderIterator->locationId) {
									for (auto locationSpreaderID = locationIterator->spreaderIds.begin(); locationSpreaderID != locationIterator->spreaderIds.end(); ++locationSpreaderID) {
										if (*locationSpreaderID == spreaderID) {
											locationIterator->spreaderIds.erase(locationSpreaderID);
											break;
										}
									}
									break;
								}
							}
							for (auto locationIterator = locations.begin(); locationIterator != locations.end(); ++locationIterator) {
								if (locationIterator->id==locationID) {
									locationIterator->spreaderIds.push_back(spreaderID);
								}
							}
							spreaderIterator->locationId = locationID;
							spreaderIterator->lastSeen = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
							added = true;
							break;
						}
					}
					if (!added) {
						//spreaderLocationPairs.push_back(std::pair<int, int>(spreaderID, locationID));
						for (auto sprdr = spreaders.begin(); sprdr != spreaders.end(); ++sprdr) {
							if (sprdr->id == spreaderID) {
								sprdr->locationId = locationID;
								sprdr->lastSeen = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());//set to current time
							}
						}
						for (auto lction = locations.begin(); lction != locations.end)
					}
					std::cout << "added data pair" << std::endl;
				}
				else if ((*i)->instruction == 0x03) { //return location of requested spreader
					int* pSpreaderID = reinterpret_cast<int*>((*i)->dataLocation);
					for (std::vector<std::pair<int, int>>::iterator pair = spreaderLocationPairs.begin(); pair != spreaderLocationPairs.end(); ++pair) {
						if (pair->first == *pSpreaderID) {
							*pSpreaderID = pair->second;
							break;
						}
					}
				}
				else if ((*i)->instruction == 0x04) { //get first spreader asociated with this location(needs more thaught put into returning extra values)
					int* pLocationID = reinterpret_cast<int*>((*i)->dataLocation);
					for (std::vector<std::pair<int, int>>::iterator pair = spreaderLocationPairs.begin(); pair != spreaderLocationPairs.end(); ++pair) {
						if (pair->second == *pLocationID) {
							*pLocationID = pair->first;
							break;
						}
					}
				}
				else if ((*i)->instruction == 0x05) { //add spreader to lis of known spreaders
					int* pSpreaderId = reinterpret_cast<int*>((*i)->dataLocation);
					char* name = reinterpret_cast<char*>(pSpreaderId + 1);//the name is stored directly after the int id
					Spreader sprdr;
					sprdr.id = *pSpreaderId;
					sprdr.lastSeen = 0;
					sprdr.locationId = 0;//NOWHERE
					strcpy_s(sprdr.name, maxSpreaderNameSize, name);
				}
				else if ((*i)->instruction == 0x05) { //add location to list of known locations
					int* pLocationId = reinterpret_cast<int*>((*i)->dataLocation);
					char* name = reinterpret_cast<char*>(pLocationId + 1);//the name is stored directly after the int id
					Location sprdr;
					sprdr.id = *pLocationId;
					strcpy_s(sprdr.name, maxLocationNameSize, name);
				}
				else if ((*i)->instruction == 0xff) { //remove the socket thread from the list of requests being processed
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

void listenThread(ThreadBuffers **listenBufferslocation, std::vector<std::thread>** ppHandlerThreads, bool *isSetup)
{

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
