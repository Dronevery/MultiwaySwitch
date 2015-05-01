#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
//#include <netinet/udp.h>
#include <stdio.h>
//#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "config.h"

#define BUFFLENGTH 65536
#define MAXMINUS 1000

int LinkNum;
int LinkNow;
long long Maxtot;
int Socket[2];
//sock0:net to server  sock1:localhost
struct sockaddr_in *SendAddr;
//0,1,2,...LinkNum-1:to client; LinkNum:to local
struct sockaddr_in RecvAddr[2];

//void init_mutex();
void init_memory();
int init_sock_local(const char s_addr[], int s_port);

int init_sock_s(int LinkID, int s_port);

int SendToClient(int AddrId, char msg[], int len);

int SendToLocal(char msg[], int len);

int Recvfrom(int LinkID, char msg[]);

int UpdateAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr);

void *ForwardUDP(void *v);

void ChangeNet(int LinkNow_c);



int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s {ConfigFileName}", argv[0]);
        exit(1);
    }
    ServerConfig config = loadServer(argv[1]);
    printf("This is a UDP server, I will received message from client and reply with same message\n");
    LinkNum = config.linkCount;
    init_memory();
    init_sock_s(0, config.receivePort);//init the Socket and addr
    init_sock_local("127.0.0.1", config.localForwardPort);
    //printf("Socket init....finish\n");
    //srand(time(NULL));
    LinkNow = 0;
    Maxtot = 0;
    pthread_t pt_f;
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (true) {
        FD_ZERO(&inputs);
        FD_SET(Socket[0], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if (result <= 0)continue;
        if (FD_ISSET(Socket[0], &inputs)) {
            int msglen = Recvfrom(0, buff);
            printf("%s %u says: %s\n", inet_ntoa(RecvAddr[0].sin_addr), ntohs(RecvAddr[0].sin_port), buff);
            int pi, LinkNow_c;
            pi = buff[0] - '0';
            LinkNow_c = buff[1] - '0';
            if (pi < 0 || pi > LinkNum - 1 || LinkNow_c < 0 || LinkNow_c > LinkNum) {
                printf("Invalid package.\n");
                continue;
            }
            UpdateAddr(&SendAddr[pi], &RecvAddr[0]);//update destination address of this link
            if (buff[1] == '0' + LinkNum)// if the packet is a data packet, not a test packet.
            {
                printf("!!receive UDP data packet from client!: \n");
                SendToLocal(&buff[2], msglen - 2);
                continue;
            }

            //for debug, sleep to simulate net delay
            //int WaitTime = rand()%400;
            //usleep(WaitTime * 1000);

            SendToClient(pi, buff, msglen);//send msg
            printf("sent: %s\n", buff);

            long long tot;
            sscanf(&buff[2], "%lld", &tot);
            if (tot > Maxtot || (Maxtot - tot) > MAXMINUS) {
                Maxtot = tot;
                if (LinkNow_c != LinkNow) {
                    ChangeNet(LinkNow_c);
                }
            }
        }
    }
}

void init_memory(){
    SendAddr = (struct sockaddr_in *)calloc((size_t)LinkNum + 1, sizeof(struct sockaddr_in));
}
void ChangeNet(int LinkNow_c) {
    LinkNow = LinkNow_c;
    printf("!!!!!!!!!!!!!!!!!!!!!Data Link changed to link%d\n", LinkNow);
}

void *ForwardUDP(void *v) {
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (1) {
        FD_ZERO(&inputs);
        FD_SET(Socket[1], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if (result <= 0)continue;
        if (FD_ISSET(Socket[1], &inputs)) {
            int msglen = Recvfrom(1, &buff[1]);
            printf("!>>Get UDP from local port<<!\n");
            buff[0] = '0' + LinkNum;
            //printf("%s\n", buff);
            int LinkNow_t = LinkNow;
            SendToClient(LinkNow_t, buff, msglen + 1);
        }
    }
}

int init_sock_local(const char s_addr[], int s_port) {
    if ((Socket[1] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    SendAddr[LinkNum].sin_family = AF_INET;
    SendAddr[LinkNum].sin_port = htons(s_port);
    SendAddr[LinkNum].sin_addr.s_addr = inet_addr(s_addr);
    if (SendAddr[LinkNum].sin_addr.s_addr == INADDR_NONE) {
        printf("Incorrect ip address!\n");
        close(Socket[1]);
        exit(1);
    }
    return 0;
}

int init_sock_s(int LinkID, int s_port) {

    if ((Socket[LinkID] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(Socket[LinkID], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("init_socket_bind");
        exit(1);
    }
    return 0;
}

int SendToClient(int AddrId, char msg[], int len) {
    int n;
    n = sendto(Socket[0], msg, len, 0, (struct sockaddr *) &(SendAddr[AddrId]), sizeof(SendAddr[AddrId]));
    if (n < 0) {
        perror("sendto");
        //close(Socket[LinkID]);
        /* print error but continue */
        return -1;
    }
    return 0;
}

int SendToLocal(char msg[], int len) {
    int n;
    n = sendto(Socket[1], msg, len, 0, (struct sockaddr *) &(SendAddr[LinkNum]), sizeof(SendAddr[LinkNum]));
    if (n < 0) {
        perror("sendto");
        //close(Socket[LinkID]);
        /* print error but continue */
        return -1;
    }
    return 0;
}

int Recvfrom(int LinkID, char msg[]) //recvfrom for client
{
    int addr_len = sizeof(RecvAddr[LinkID]);
    int n;
    n = recvfrom(Socket[LinkID], msg, BUFFLENGTH, MSG_DONTWAIT, (struct sockaddr *) &(RecvAddr[LinkID]),
                 (socklen_t *) &addr_len);
    if (n > 0) {
        msg[n] = 0;
    }
    else {
        msg[0] = 0;//clear the string if failed
    }
    return n;
}

int UpdateAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr) {
    AddrNow->sin_family = NewAddr->sin_family;
    AddrNow->sin_port = NewAddr->sin_port;
    AddrNow->sin_addr = NewAddr->sin_addr;
    return 0;
}

