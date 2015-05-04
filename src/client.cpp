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
#include "config.h"

#define BUFFLENGTH 65536
#define ARRLENGTH 10
#define MAXFAIL 3
#define MINFAIL 2
//#define MAXARGC 12

struct netflag {
    int arr[ARRLENGTH];
    int p, tot;
};
struct param {
    int id;
};

pthread_mutex_t tot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t switch_mutex = PTHREAD_MUTEX_INITIALIZER;   //for SwitchLink()

int totalLinkNum, *linkPriority, *netStatus;
int *Socket;
//sock0,1,2,..linkNum-1:link to server  sockLinkNum:localhost
struct sockaddr_in *SendAddr;
struct sockaddr_in *RecvAddr;
//struct sockaddr_in SendAddr[4];
//struct sockaddr_in RecvAddr[4];
int currLinkNow;

void LoadIpVariable(ClientConfig *config);

void initMutex();

void initMemory();

int initNetflag(struct netflag *a);

int updateNetflag
        (struct netflag *a, int flag);

int init_sock_c(int LinkId, const char c_addr[], int c_port, const char s_addr[], int s_port);

int init_sock_s(int LinkId, int s_port);

int Sendto(int LinkId, char msg[], int len);

int Recvfrom(int LinkId, char msg[]);

int waitRecv(int LinkId, char msg[], int WaitTime);

void waitRecvData(int LinkId, int WaitTime);

void *TestLink(void *v);

void *SwitchLink(void *v);

void *ForwardUDP(void *v);

void ChangeNet(int bestlink);

struct param *make_param(int id);


int main(int argc, char **argv) {
    ClientConfig config;
    if (argc != 2) {
        printf("Read config file from default path...\n");
        config = loadClient("/etc/multiswitch/client.ini");
    } else {
        config = loadClient(argv[1]);
    }
    totalLinkNum = config.linkCount;
    LoadIpVariable(&config);
    printf("This is a UDP client\n");

    initMemory();  //pre-locate global array
    initMutex();   //initialize all mutex

    currLinkNow = 0;
    for (int i = 0; i < totalLinkNum; i++) {
        netStatus[i] = 1;
        linkPriority[i] = config.links[i].priority;
        init_sock_c(i, config.links[i].sourceAddress.c_str(), config.links[i].sourcePort,
                    config.links[i].destinationAddress.c_str(), config.destinationPort);
    }
    init_sock_s(totalLinkNum, config.localForwardPort);
    pthread_t *pt_t;
    pt_t = new pthread_t[totalLinkNum];
    pthread_t pt_s, pt_f;
    pthread_create(&pt_s, NULL, SwitchLink, NULL);
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);

    for (int i = 0; i < totalLinkNum; i++) {
        struct param *para = make_param(i);
        pthread_create(&pt_t[i], NULL, TestLink, (void *) para);
    }

    for (int i = 0; i < totalLinkNum; i++) {
        pthread_join(pt_t[i], NULL);
    }
    return 0;
}

void initMemory() {
    linkPriority = new int[totalLinkNum];
    netStatus = new int[totalLinkNum];
    Socket = new int[totalLinkNum + 1];
    SendAddr = new sockaddr_in[totalLinkNum + 1];
    RecvAddr = new sockaddr_in[totalLinkNum + 1];
}

void LoadIpVariable(ClientConfig *config) {
    extern char **environ;
    int i, j;
    for (i = 0; environ[i]; i++) {
        for (j = 0; j < totalLinkNum; j++) {
            char VarName[20];
            sprintf(VarName, "IP_link%d", j);
            if (strstr(environ[i], VarName) == environ[i]) {
                printf("Load addr1 from %s\n", VarName);
                char IPAddr[20];
                strcpy(IPAddr, environ[i] + strlen(VarName));
                config->links[j].sourceAddress = IPAddr;
            }
        }
    }
}

void *TestLink(void *v) {
    struct param *para = (param *) v;
    int id = para->id;
    struct netflag Flags;
    static int tot = 0;
    initNetflag(&Flags);

    char msg[BUFFLENGTH];
    while (true) {
        pthread_mutex_lock(&tot_mutex);
        tot++;
        sprintf(msg, "%d%c%u", id, '0' + currLinkNow, tot);//prepare msg
        pthread_mutex_unlock(&tot_mutex);
        Sendto(id, msg, strlen(msg));//send msg
        int flag = waitRecv(id, msg, 1000000);//wait 1s for response
        updateNetflag(&Flags, flag);
        printf("Link%d %s  Statue: %d/%d \n", id, (flag ? ("Success.") : ("Failed. ")), Flags.tot, ARRLENGTH);
        if (ARRLENGTH - Flags.tot >= MAXFAIL && netStatus[id] == 1) {
            netStatus[id] = 0;//close
            pthread_mutex_unlock(&switch_mutex);//let SwitchLink runs~
        }
        else if (ARRLENGTH - Flags.tot <= MINFAIL && netStatus[id] == 0) {
            netStatus[id] = 1;//good
            pthread_mutex_unlock(&switch_mutex);//let SwitchLink runs~
        }
        if (flag) waitRecvData(id, 100000);
        //usleep(500000);
    }

    return 0;
}


void *SwitchLink(void *v) {
    while (true) {
        pthread_mutex_lock(&switch_mutex);

        printf("!!!SwitchLink runs~  Netstatus: ");
        int i;
        for (i = 0; i < totalLinkNum; i++)
            if (netStatus[i] == 0) printf("Closed.");
            else printf("Good.  ");
        printf("\n");

        int bestlink, MaxPriority = -1;
        for (i = 0; i < totalLinkNum; i++)
            if (netStatus[i] != 0 && linkPriority[i] > MaxPriority) {
                MaxPriority = linkPriority[i];
                bestlink = i;
            }
        if (MaxPriority > linkPriority[currLinkNow]) ChangeNet(bestlink);
    }
}

void ChangeNet(int bestlink) {

    pthread_mutex_lock(&tot_mutex);
    currLinkNow = bestlink;
    /*
    Code to change the network
    */
    pthread_mutex_unlock(&tot_mutex);

    printf("!!!!!!!!!!!!!!!!!!!!!Data Link changed to link%d\n", currLinkNow);
}

void *ForwardUDP(void *v) {
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (true) {
        FD_ZERO(&inputs);
        FD_SET(Socket[totalLinkNum], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if (result <= 0)continue;
        if (FD_ISSET(Socket[totalLinkNum], &inputs)) {
            int msglen = Recvfrom(totalLinkNum, &buff[2]);
            printf("!>>Get UDP from local port<<!\n");
            int LinkNow_t = currLinkNow;
            buff[1] = '0' + totalLinkNum;
            buff[0] = '0' + LinkNow_t;
            //printf("%s\n", buff);
            Sendto(LinkNow_t, buff, msglen + 2);
        }
    }
}

void initMutex()//initialize all mutex
{
    pthread_mutex_init(&tot_mutex, NULL);
    pthread_mutex_init(&switch_mutex, NULL);//for SwitchLink()
}

struct param *make_param(int id) {
    auto tmp = new param;
    tmp->id = id;
    return tmp;
}

int initNetflag(struct netflag *a) {
    int i;
    for (i = 0; i < ARRLENGTH; i++) {
        a->arr[i] = 1;
    }
    a->p = 0;
    a->tot = ARRLENGTH;
    return 0;
}

int updateNetflag(struct netflag *a, int flag) {
    a->tot -= a->arr[a->p];
    a->arr[a->p] = flag != 0;
    a->tot += a->arr[a->p];
    a->p = (a->p + 1) % ARRLENGTH;
    return 0;
}

int init_sock_c(int LinkId, const char c_addr[], int c_port, const char s_addr[], int s_port) {

    if ((Socket[LinkId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(c_port);
    addr.sin_addr.s_addr = inet_addr(c_addr);

    if (::bind(Socket[LinkId], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("init_socket_bind");
        exit(1);
    }

    SendAddr[LinkId].sin_family = AF_INET;
    SendAddr[LinkId].sin_port = htons(s_port);
    SendAddr[LinkId].sin_addr.s_addr = inet_addr(s_addr);
    if (SendAddr[LinkId].sin_addr.s_addr == INADDR_NONE) {
        printf("Incorrect ip address!\n");
        close(Socket[LinkId]);
        exit(1);
    }
    return 0;
}

int init_sock_s(int LinkId, int s_port) {

    if ((Socket[LinkId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(Socket[LinkId], (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("init_socket_bind");
        exit(1);
    }
    return 0;
}

int Sendto(int LinkId, char msg[], int len) // sendto for client
{
    int n;
    n = (int) sendto(Socket[LinkId], msg, (size_t) len, 0, (struct sockaddr *) &(SendAddr[LinkId]),
                     sizeof(SendAddr[LinkId]));
    if (n < 0) {
        perror("sendto");
        //close(Socket[LinkId]);
        /* print error but continue */
        return -1;
    }
    return 0;
}

int Recvfrom(int LinkId, char msg[]) //recvfrom for client
{
    int addr_len = sizeof(RecvAddr[LinkId]);
    int n;
    n = (int) recvfrom(Socket[LinkId], msg, BUFFLENGTH, MSG_DONTWAIT, (struct sockaddr *) &(RecvAddr[LinkId]),
                       (socklen_t *) &addr_len);
    if (n > 0) {
        msg[n] = 0;
    }
    else {
        msg[0] = 0;//clear the string if failed
    }
    return n;
}

int waitRecv(int LinkId, char msg[], int WaitTime)// Wait UDP response for WaitTime usec
{
    static char buff[BUFFLENGTH];
    fd_set inputs;
    struct timeval timeout;
    FD_ZERO(&inputs);
    FD_SET(Socket[LinkId], &inputs);
    timeout.tv_sec = WaitTime / 1000000;
    timeout.tv_usec = WaitTime % 1000000;
    while (timeout.tv_usec > 0 || timeout.tv_sec > 0) {
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, &timeout);
        //printf("%d : %d : %d\n", result, (int)timeout.tv_sec, (int)timeout.tv_usec);
        if (result <= 0) return 0;
        int n = Recvfrom(LinkId, buff);
        if (buff[0] == '0' + totalLinkNum) {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[totalLinkNum].sin_family = RecvAddr[totalLinkNum].sin_family;
            SendAddr[totalLinkNum].sin_port = RecvAddr[totalLinkNum].sin_port;
            SendAddr[totalLinkNum].sin_addr.s_addr = RecvAddr[totalLinkNum].sin_addr.s_addr;
            Sendto(totalLinkNum, &buff[1], n - 1);

            continue;
        }
        if ((n > 0) && (strcmp(msg, buff) == 0))
            return 1;
    }
    return 0;
}

void waitRecvData(int LinkId, int WaitTime)// Wait UDP data package for WaitTime usec
{
    static char buff[BUFFLENGTH];
    fd_set inputs;
    struct timeval timeout;
    FD_ZERO(&inputs);
    FD_SET(Socket[LinkId], &inputs);
    timeout.tv_sec = WaitTime / 1000000;
    timeout.tv_usec = WaitTime % 1000000;
    while (timeout.tv_usec > 0 || timeout.tv_sec > 0) {
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, &timeout);
        //printf("%d : %d : %d\n", result, (int)timeout.tv_sec, (int)timeout.tv_usec);
        if (result <= 0) return;
        int n = Recvfrom(LinkId, buff);
        if (buff[0] == '0' + totalLinkNum) {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[totalLinkNum].sin_family = RecvAddr[totalLinkNum].sin_family;
            SendAddr[totalLinkNum].sin_port = RecvAddr[totalLinkNum].sin_port;
            SendAddr[totalLinkNum].sin_addr.s_addr = RecvAddr[totalLinkNum].sin_addr.s_addr;
            Sendto(totalLinkNum, &buff[1], n - 1);
        }
    }
}
