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

pthread_mutex_t sock_mutex[4] = {//for asock[4]
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER
};


int PortNow, Maxtot; 
int Socket[4];//sock0,1,2:net to server  sock3:localhost
struct sockaddr_in SendAddr[4];
struct sockaddr_in RecvAddr[4];

void init_mutex();
int init_sock_c(int PortId, char s_addr[], char s_port[]);
int init_sock_s(int PortId,  char s_port[]);
int Sendto(int PortId, char msg[]);
int Recvfrom(int PortId, char msg[]);
int wait_recv(int PortId, char msg[], int WaitTime);
int CheckAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr);
void * ForwardUDP(void * v);
void ChangeNet(int bestport);


int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: %s port port port port\nThe 4th port is the local port\n", argv[0]);
        exit(1);
    }
    printf("This is a UDP server, I will received message from client and reply with same message\n");


    int pi;
    for(pi = 0; pi < 3; pi++)
    {
        init_sock_s(pi, argv[1+pi]);//init the Socket and addr
        SendAddr[pi].sin_port = 0;  //as a NULL flag means that addr has never been assigned
    }
    //printf("Socket init....finish\n");

    srand(time(NULL));

    PortNow = 0;
    Maxtot = 0;

    fd_set allPortSet;
    fd_set inputs;
    FD_ZERO(&allPortSet);
    for(pi = 0; pi < 3; pi++) 
    {
        FD_SET(Socket[pi], &allPortSet);
    }

    char buff[BUFFLENGTH];
    while (1)
    {
        FD_ZERO(&inputs);
        inputs = allPortSet;
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if(result <= 0)continue;
        for(pi = 0; pi < 3; pi++)
        if(FD_ISSET(Socket[pi], &inputs))
        {
            Recvfrom(pi, buff);
            printf("%s %u says: %s\n", inet_ntoa(RecvAddr[pi].sin_addr), ntohs(RecvAddr[pi].sin_port), buff);
            
            //SendAddr[pi] == RecvAddr[pi];
            if(CheckAddr(&SendAddr[pi], &RecvAddr[pi]) != 0) continue;   //check if the UDP packet come from legal address

            //for debug, sleep to simulate delay
            int WaitTime = rand()%400;
            usleep(WaitTime * 1000);

            Sendto(pi, buff);//send msg
            printf("sent: %s\n", buff);

            int port, tot;
            port = buff[0]-'0';
            sscanf(&buff[1], "%d", &tot);
            //printf("%d : %d\n", tot, port);
            if(tot > Maxtot || (Maxtot - tot) > MAXMINUS)
            {
                Maxtot = tot;
                if(port != PortNow)
                {
                    ChangeNet(port);
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
        FD_SET(Socket[3], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if(result <= 0)continue;
        if(FD_ISSET(Socket[3], &inputs))
        {
            pthread_mutex_lock(&sock_mutex[3]);
            Recvfrom(3, &buff[1]);
            pthread_mutex_unlock(&sock_mutex[3]);

            printf("!>>Get UDP from local port<<!\n");

            buff[0] = '3';
            printf("%s\n", buff);

            int PortNow_t = PortNow;
            pthread_mutex_lock(&sock_mutex[PortNow_t]);
            Sendto(PortNow_t, buff);
            pthread_mutex_unlock(&sock_mutex[PortNow_t]);
        }
    }
    return 0;
}

void init_mutex()//initialize all mutex
{
    int pi;
    for(pi = 0; pi < 4; pi++)
    {
        pthread_mutex_init(&sock_mutex[pi], NULL);
    }
}

int init_sock_c(int PortId, char s_addr[], char s_port[])
{

    if((Socket[PortId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("init_socket");
        exit(1);
    }
    SendAddr[PortId].sin_family = AF_INET;
    SendAddr[PortId].sin_port = htons(atoi(s_port));
    SendAddr[PortId].sin_addr.s_addr = inet_addr(s_addr);
    if(SendAddr[PortId].sin_addr.s_addr == INADDR_NONE)
    {
        printf("Incorrect ip address!\n");
        close(Socket[PortId]);
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
int Sendto(int PortId, char msg[]) // sendto for client
{
    int n;
    n = sendto(Socket[PortId], msg, strlen(msg), 0, (struct sockaddr *)&(SendAddr[PortId]), sizeof(SendAddr[PortId]));
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

int CheckAddr(struct sockaddr_in *AddrNow, struct sockaddr_in *NewAddr)
{
    if((AddrNow->sin_port != 0) && (AddrNow->sin_addr.s_addr != NewAddr->sin_addr.s_addr))
        return -1;
    AddrNow->sin_family = NewAddr->sin_family;
    AddrNow->sin_port = NewAddr->sin_port;
    AddrNow->sin_addr = NewAddr->sin_addr;
    return 0;
}