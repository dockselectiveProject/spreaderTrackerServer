#pragma once

#include <list>


bool initSockets();
//socket id system

//send receve system
//error value detection
//close socket
//socket creation, listening, binding
int socketCreate();

bool socketListenOn(int);

bool socketBind(int, int);

int socketAcceptClient(int);

void socketSend(int, char*);

void socketRecv(int, char*);

void closeSocket(int);

