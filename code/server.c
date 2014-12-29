#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define BUFFLENGTH 65536
#define MAXMINUS 1000

int PortNow;
long long Maxtot; 
int Socket[2];//sock0:net to server  sock1:localhost
struct sockaddr_in SendAddr[4];//0,1,2:to client; 3:to local
struct sockaddr_in RecvAddr[2];

//void init_mutex();
int init_sock_local(char s_addr[], char s_port[]);
int init_sock_s(int PortId,  char s_port[]);
int SendToClient(int AddrId, char msg[], int len);
int SendToLocal(char msg[], int len);
int Recvfrom(int PortId, char msg[]);
int UpdateAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr);
void * ForwardUDP(void * v);
void ChangeNet(int bestport);

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s receivePort fowardtoPort\n", argv[0]);
        exit(1);
    }
    printf("This is a UDP server, I will received message from client and reply with same message\n");
    init_sock_s(0, argv[1]);//init the Socket and addr
    init_sock_local("127.0.0.1", argv[2]);
    //printf("Socket init....finish\n");
    srand(time(NULL));
    PortNow = 0;
    Maxtot = 0;
    pthread_t pt_f;
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (1)
    {
        FD_ZERO(&inputs);
        FD_SET(Socket[0], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if(result <= 0)continue;
        if(FD_ISSET(Socket[0], &inputs))
        {
            int msglen = Recvfrom(0, buff);
            printf("%s %u says: %s\n", inet_ntoa(RecvAddr[0].sin_addr), ntohs(RecvAddr[0].sin_port), buff);
            int pi, PortNow_c;
            pi = buff[0] - '0';
            PortNow_c = buff[1] - '0';
            if(pi < 0 || pi > 2 || PortNow_c < 0 || PortNow_c > 3)
            {
                printf("Invalid package.\n");
                continue;
            }
            UpdateAddr(&SendAddr[pi], &RecvAddr[0]);//update destination address of this link
            if(buff[1] == '3')// if the packet is a data packet, not a test packet.
            {
                printf("!!receive UDP data packet from client!: %s\n", buff);
                SendToLocal(&buff[2], msglen-2);
                continue;
            }

            //for debug, sleep to simulate net delay
            int WaitTime = rand()%400;
            usleep(WaitTime * 1000);

            SendToClient(pi, buff, msglen);//send msg
            printf("sent: %s\n", buff);

            long long tot;
            sscanf(&buff[2], "%lld", &tot);
            //printf("%d : %d\n", tot, port);
            if(tot > Maxtot || (Maxtot - tot) > MAXMINUS)
            {
                Maxtot = tot;
                if(PortNow_c != PortNow)
                {
                    ChangeNet(PortNow_c);
                }
            }
        }
    }
    return 0;
}

void ChangeNet(int bestport)
{
    PortNow = bestport;
    printf("!!!!!!!!!!!!!!!!!!!!!Control Port changed to port%d\n", PortNow);
}

void * ForwardUDP(void *v)
{
    fd_set inputs;
    char buff[BUFFLENGTH];
    while(1)
    {
        FD_ZERO(&inputs);
        FD_SET(Socket[1], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if(result <= 0)continue;
        if(FD_ISSET(Socket[1], &inputs))
        {
            int msglen = Recvfrom(1, &buff[1]);
            printf("!>>Get UDP from local port<<!\n");
            buff[0] = '3';
            printf("%s\n", buff);
            int PortNow_t = PortNow;
            SendToClient(PortNow_t, buff, msglen+1);
        }
    }
    return 0;
}

int init_sock_local(char s_addr[], char s_port[])
{
    if((Socket[1] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("init_socket");
        exit(1);
    }
    SendAddr[3].sin_family = AF_INET;
    SendAddr[3].sin_port = htons(atoi(s_port));
    SendAddr[3].sin_addr.s_addr = inet_addr(s_addr);
    if(SendAddr[3].sin_addr.s_addr == INADDR_NONE)
    {
        printf("Incorrect ip address!\n");
        close(Socket[1]);
        exit(1);
    }
    return 0;
}
int init_sock_s(int PortId, char s_port[])
{
    
    if((Socket[PortId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(s_port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if(bind(Socket[PortId], (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("init_socket_bind");
        exit(1);
    }
    return 0;
}
int SendToClient(int AddrId, char msg[], int len)
{
    int n;
    n = sendto(Socket[0], msg, len, 0, (struct sockaddr *)&(SendAddr[AddrId]), sizeof(SendAddr[AddrId]));
    if (n < 0)
    {
        perror("sendto");
        //close(Socket[PortId]);
        /* print error but continue */
        return -1;
    }
    return 0;
}
int SendToLocal(char msg[], int len)
{
    int n;
    n = sendto(Socket[1], msg, len, 0, (struct sockaddr *)&(SendAddr[3]), sizeof(SendAddr[3]));
    if (n < 0)
    {
        perror("sendto");
        //close(Socket[PortId]);
        /* print error but continue */
        return -1;
    }
    return 0;
}

int Recvfrom(int PortId, char msg[]) //recvfrom for client
{
    int addr_len = sizeof(RecvAddr[PortId]);
    int n;
    n = recvfrom(Socket[PortId], msg, BUFFLENGTH, MSG_DONTWAIT, (struct sockaddr *)&(RecvAddr[PortId]), (socklen_t *)&addr_len);
    if (n>0)
        {
            msg[n] = 0;
        }
    else
        {
            msg[0] = 0;//clear the string if failed
        }
    return n;
}

int UpdateAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr)
{
    AddrNow->sin_family = NewAddr->sin_family;
    AddrNow->sin_port = NewAddr->sin_port;
    AddrNow->sin_addr = NewAddr->sin_addr;
    return 0;
}

