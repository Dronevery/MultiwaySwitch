#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
//#include <netinet/udp.h>
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

int LinkNum;        //number of Links
//int configArgc;
//char configArgv[MAXARGC][20];

int *link_priority;
//int netstatus[3];
int *netstatus;
unsigned int tot;

//int Socket[4];
int *Socket;
//sock0,1,2,..LinkNum-1:link to server  sockLinkNum:localhost
struct sockaddr_in *SendAddr;
struct sockaddr_in *RecvAddr;
//struct sockaddr_in SendAddr[4];
//struct sockaddr_in RecvAddr[4];
int LinkNow;

//void LoadClientConfig(const char fileName[]);

void LoadIpVariable(ClientConfig *config);

void init_mutex();

void init_memory();

int init_netflag(struct netflag *a);

int update_netflag(struct netflag *a, int flag);

int init_sock_c(int LinkId, const char c_addr[], int c_port, const char s_addr[], int s_port);

int init_sock_s(int LinkId, int s_port);

int Sendto(int LinkId, char msg[], int len);

int Recvfrom(int LinkId, char msg[]);

int wait_recv(int LinkId, char msg[], int WaitTime);

void wait_recv_data(int LinkId, int WaitTime);

void *TestLink(void *v);

void *SwitchLink(void *v);

void *ForwardUDP(void *v);

void ChangeNet(int bestlink);

struct param *make_param(int id);


int main(int argc, char **argv) {
    ClientConfig config;
    if (argc != 2) {
        //printf("Usage: %s {ConfigFileName}", argv[0]);
        //exit(1);
        printf("Read config file from default path...\n");
        config = loadClient("/etc/multiswitch/client.ini");
    } else config = loadClient(argv[1]);
    LinkNum = config.linkCount;
    //LoadClientConfig("config_c.txt");
    LoadIpVariable(&config);
    printf("This is a UDP client\n");
    
    init_memory();
    init_mutex();   //initialize all mutex
    int pi;
    tot = 0;
    LinkNow = 0;
    for (pi = 0; pi < LinkNum; pi++) {
        netstatus[pi] = 1;
        link_priority[pi] = config.links[pi].priority;
        init_sock_c(pi, config.links[pi].sourceAddress.c_str(), config.links[pi].sourcePort, config.links[pi].destinationAddress.c_str(), config.destinationPort);
    }
    init_sock_s(LinkNum, config.localForwardPort);
    pthread_t *pt_t;
    pt_t = (pthread_t *)calloc((size_t)LinkNum, sizeof(pthread_t));
    pthread_t pt_s, pt_f;
    pthread_create(&pt_s, NULL, SwitchLink, NULL);
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);

    for (pi = 0; pi < LinkNum; pi++) {

        struct param *para = make_param(pi);
        pthread_create(&pt_t[pi], NULL, TestLink, (void *) para);
    }


    for (pi = 0; pi < LinkNum; pi++) {
        pthread_join(pt_t[pi], NULL);
    }
    return 0;
}

void init_memory(){
    link_priority = (int *)calloc((size_t)LinkNum, sizeof(int));
    netstatus = (int *)calloc((size_t)LinkNum, sizeof(int));
    Socket = (int *)calloc((size_t)LinkNum + 1, sizeof(int));
    SendAddr = (struct sockaddr_in *)calloc((size_t)LinkNum + 1, sizeof(struct sockaddr_in));
    RecvAddr = (struct sockaddr_in *)calloc((size_t)LinkNum + 1, sizeof(struct sockaddr_in));
}

/*void LoadClientConfig(const char fileName[]) {
    printf("Loading config from file %s ...", fileName);
    FILE *configFile;
    if ((configFile = fopen(fileName, "r")) == NULL) {
        printf("Error:Config file does not exist\n");
        exit(0);
    }
    char inputString[20];
    configArgc = 0;
    while (fscanf(configFile, "%s", inputString) > 0) {
        if (configArgc == MAXARGC) break;
        strcpy(configArgv[configArgc], inputString);
        configArgc++;
    }
    printf("finished\n");
    //int i;for(i=0;i<configArgc;i++)printf("%s ", configArgv[i]);
}*/

void LoadIpVariable(ClientConfig *config) {
    extern char **environ;
    int i, j;
    for (i = 0; environ[i]; i++) {
        for (j = 0; j < LinkNum ;j++){
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
    init_netflag(&Flags);

    char msg[BUFFLENGTH];
    while (true) {
        pthread_mutex_lock(&tot_mutex);
        tot++;
        sprintf(msg, "%d%c%u", id, '0' + LinkNow, tot);//prepare msg
        pthread_mutex_unlock(&tot_mutex);
        Sendto(id, msg, strlen(msg));//send msg
        //printf("sent: %s\n", msg);

        int flag = wait_recv(id, msg, 1000000);//wait 1s for response

        update_netflag(&Flags, flag);

        printf("Link%d %s  Statue: %d/%d \n", id, (flag ? ("Success.") : ("Failed. ")), Flags.tot, ARRLENGTH);

        if (ARRLENGTH - Flags.tot >= MAXFAIL && netstatus[id] == 1) {
            netstatus[id] = 0;//close
            pthread_mutex_unlock(&switch_mutex);//let SwitchLink runs~
        }
        else if (ARRLENGTH - Flags.tot <= MINFAIL && netstatus[id] == 0) {
            netstatus[id] = 1;//good
            pthread_mutex_unlock(&switch_mutex);//let SwitchLink runs~
        }
        if (flag) wait_recv_data(id, 100000);
        //usleep(500000);
    }

    return 0;
}


void *SwitchLink(void *v) {
    while (true) {
        pthread_mutex_lock(&switch_mutex);

        printf("!!!SwitchLink runs~  Netstatus: ");
        int i;
        for (i = 0; i < LinkNum; i++)
            if (netstatus[i] == 0) printf("Closed.");
            else printf("Good.  ");
        printf("\n");

        int bestlink, MaxPriority = -1;
        for (i = 0; i < LinkNum; i++)
            if (netstatus[i] != 0 && link_priority[i] > MaxPriority) {
                MaxPriority = link_priority[i];
                bestlink = i;
            }
        if (MaxPriority > link_priority[LinkNow]) ChangeNet(bestlink);
    }
}

void ChangeNet(int bestlink) {

    pthread_mutex_lock(&tot_mutex);
    LinkNow = bestlink;
    /*
    Code to change the network
    */
    pthread_mutex_unlock(&tot_mutex);

    printf("!!!!!!!!!!!!!!!!!!!!!Data Link changed to link%d\n", LinkNow);
}

void *ForwardUDP(void *v) {
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (true) {
        FD_ZERO(&inputs);
        FD_SET(Socket[LinkNum], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if (result <= 0)continue;
        if (FD_ISSET(Socket[LinkNum], &inputs)) {
            int msglen = Recvfrom(LinkNum, &buff[2]);


            printf("!>>Get UDP from local port<<!\n");

            int LinkNow_t = LinkNow;
            buff[1] = '0' + LinkNum;
            buff[0] = '0' + LinkNow_t;
            //printf("%s\n", buff);
            Sendto(LinkNow_t, buff, msglen + 2);

        }
    }
}

void init_mutex()//initialize all mutex
{
    pthread_mutex_init(&tot_mutex, NULL);
    pthread_mutex_init(&switch_mutex, NULL);//for SwitchLink()
}

struct param *make_param(int id) {
    struct param *a = (param *) malloc(sizeof(struct param));
    a->id = id;
    return a;
}

int init_netflag(struct netflag *a) {
    int i;
    for (i = 0; i < ARRLENGTH; i++) {
        a->arr[i] = 1;
    }
    a->p = 0;
    a->tot = ARRLENGTH;
    return 0;
}

int update_netflag(struct netflag *a, int flag) {
    a->tot -= a->arr[a->p];
    a->arr[a->p] = flag != 0;
    a->tot += a->arr[a->p];
    a->p = (a->p + 1) % ARRLENGTH;
    return 0;
}

int init_sock_c(int LinkId, const char c_addr[], int c_port, const char s_addr[], int s_port){

    if ((Socket[LinkId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(c_port);
    addr.sin_addr.s_addr = inet_addr(c_addr);

    if (bind(Socket[LinkId], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
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

    if (bind(Socket[LinkId], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("init_socket_bind");
        exit(1);
    }
    return 0;
}

int Sendto(int LinkId, char msg[], int len) // sendto for client
{
    int n;
    n = (int) sendto(Socket[LinkId], msg, (size_t)len, 0, (struct sockaddr *) &(SendAddr[LinkId]), sizeof(SendAddr[LinkId]));
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

int wait_recv(int LinkId, char msg[], int WaitTime)// Wait UDP response for WaitTime usec
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
        if (buff[0] == '0' + LinkNum) {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[LinkNum].sin_family = RecvAddr[LinkNum].sin_family;
            SendAddr[LinkNum].sin_port = RecvAddr[LinkNum].sin_port;
            SendAddr[LinkNum].sin_addr.s_addr = RecvAddr[LinkNum].sin_addr.s_addr;
            Sendto(LinkNum, &buff[1], n - 1);

            continue;
        }
        if ((n > 0) && (strcmp(msg, buff) == 0))
            return 1;
    }
    return 0;
}

void wait_recv_data(int LinkId, int WaitTime)// Wait UDP data package for WaitTime usec
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
        if (buff[0] == '0' + LinkNum) {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[LinkNum].sin_family = RecvAddr[LinkNum].sin_family;
            SendAddr[LinkNum].sin_port = RecvAddr[LinkNum].sin_port;
            SendAddr[LinkNum].sin_addr.s_addr = RecvAddr[LinkNum].sin_addr.s_addr;
            Sendto(LinkNum, &buff[1], n - 1);
        }
    }
}
