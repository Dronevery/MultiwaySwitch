#include "inih/cpp/INIReader.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct ClientLink{
    string sourceAddress;
    int sourcePort;
    string destinationAddress;
    int priority;
};

struct ClientConfig{
    int destinationPort;
    int localForwardPort;
    int linkCount;
    vector<ClientLink> links;
};



struct ServerConfig{
    int receivePort;
    int localForwardPort;
    int linkCount;
};


ServerConfig loadServer(const char configFile[]);
ClientConfig loadClient(const char configFile[]);
