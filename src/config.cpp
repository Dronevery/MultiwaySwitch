#include "config.h"

ServerConfig loadServer(const char configFile[])
{
    INIReader reader(configFile);

    if (reader.ParseError() < 0) {
        cerr << "Can't load config file\n";
        exit(1);
    }

    ServerConfig config;

    config.receivePort = (int)reader.GetInteger("common","ReceivePort",10624);
    config.localForwardPort = (int)reader.GetInteger("common","LocalForwardPort",20624);
    config.linkCount = (int)reader.GetInteger("common","LinkCount",-1);

    return config;

}

ClientConfig loadClient(const char configFile[]){
    INIReader reader(configFile);
    ClientConfig config;
    if(reader.ParseError() < 0){
        cerr << "Can't load config file\n";
        exit(1);
    }

    config.destinationPort = (int)reader.GetInteger("common","DestinationPort",10624);
    config.localForwardPort = (int)reader.GetInteger("common","LocalForwardPort",20624);
    config.linkCount = (int)reader.GetInteger("common","LinkCount",-1);
    if (config.linkCount == -1)
    {
        cerr << "Can't read LinkCount\n";
        exit(2);
    }

    config.links.reserve((unsigned long)config.linkCount);
    for(int i=0;i<config.linkCount;i++){
        config.links[i].sourceAddress = reader.Get(string("link")+i,"SourceAddress","0.0.0.0");
        config.links[i].destinationAddress = reader.Get(string("link")+i,"DestinationAddress","127.0.0.1");
        config.links[i].sourcePort = (int)reader.GetInteger(string("link")+i,"SourcePort",6241);
        config.links[i].priority = (int)reader.GetInteger(config.links[i].sourcePort = (int)reader.GetInteger(string("link")+i,"Priority",0);
    }
    return config;

}