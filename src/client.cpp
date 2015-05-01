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
#define MAXARGC 12

struct netflag {
    int arr[ARRLENGTH];
    int p, tot;
};
struct param {
    int id;
};

pthread_mutex_t tot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t switch_mutex = PTHREAD_MUTEX_INITIALIZER;   //for SwitchPort()

int configArgc;
char configArgv[MAXARGC][20];

const int port_priority[3] = {0, 1, 2}; //means: port0 has the highest priority to be choosed, than port1,than port2 
int netstatus[3];
unsigned int tot;
struct netflag Flags;

int Socket[4];
//sock0,1,2:net to server  sock3:localhost
struct sockaddr_in SendAddr[4];
struct sockaddr_in RecvAddr[4];
int PortNow;

void LoadClientConfig(const char fileName[]);

void LoadIpVariable(char addr1[], char addr2[], char addr3[]);

void init_mutex();

int init_netflag(struct netflag *a);

int update_netflag(struct netflag *a, int flag);

int init_sock_c(int PortId, char c_addr[], char c_port[], char s_addr[], char s_port[]);

int init_sock_s(int PortId, char s_port[]);

int Sendto(int PortId, char msg[], int len);

int Recvfrom(int PortId, char msg[]);

int wait_recv(int PortId, char msg[], int WaitTime);

void wait_recv_data(int PortId, int WaitTime);

void *TestPort(void *v);

void *SwitchPort(void *v);

void *ForwardUDP(void *v);

void ChangeNet(int bestport);

struct param *make_param(int id);


int main() {

    LoadClientConfig("config_c.txt");
    LoadIpVariable(configArgv[1], configArgv[4], configArgv[7]);
    printf("This is a UDP client\n");

    init_mutex();   //initialize all mutex

    int pi;
    tot = 0;
    PortNow = 0;
    for (pi = 0; pi < 3; pi++) {
        netstatus[pi] = 1;
        init_sock_c(pi, configArgv[1 + pi * 3], configArgv[2 + pi * 3], configArgv[3 + pi * 3], configArgv[0]);
    }
    init_sock_s(3, configArgv[10]);

    pthread_t pt_t[3], pt_s, pt_f;
    pthread_create(&pt_s, NULL, SwitchPort, NULL);
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);

    for (pi = 0; pi < 3; pi++) {

        struct param *para = make_param(pi);
        pthread_create(&pt_t[pi], NULL, TestPort, (void *) para);
    }


    for (pi = 0; pi < 3; pi++) {
        pthread_join(pt_t[pi], NULL);
    }
    return 0;
}

void LoadClientConfig(const char fileName[]) {
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
}

void LoadIpVariable(char addr1[], char addr2[], char addr3[]) {
    extern char **environ;
    int i;
    for (i = 0; environ[i]; i++) {
        if (strstr(environ[i], "IP_WIFI") == environ[i]) {
            printf("Load addr1 from IP_WIFI\n");
            strcpy(addr1, environ[i] + strlen("IP_WIFI="));
        }
        if (strstr(environ[i], "IP_UNICOM") == environ[i]) {
            printf("Load addr2 from IP_UNICOM\n");
            strcpy(addr2, environ[i] + strlen("IP_UNICOM="));
        }
        if (strstr(environ[i], "IP_CMCC") == environ[i]) {
            printf("Load addr3 from IP_CMCC\n");
            strcpy(addr3, environ[i] + strlen("IP_CMCC="));
        }
    }
}

void *TestPort(void *v) {
    struct param *para = (param *) v;
    int id = para->id;
    struct netflag Flags;
    init_netflag(&Flags);

    char msg[BUFFLENGTH];
    while (true) {
        pthread_mutex_lock(&tot_mutex);
        tot++;
        sprintf(msg, "%d%d%u", id, PortNow, tot);//prepare msg
        pthread_mutex_unlock(&tot_mutex);
        Sendto(id, msg, strlen(msg));//send msg
        //printf("sent: %s\n", msg);

        int flag = wait_recv(id, msg, 1000000);//wait 1s for response

        update_netflag(&Flags, flag);

        printf("Port%d %s  Statue: %d/%d \n", id, (flag ? ("Success.") : ("Failed. ")), Flags.tot, ARRLENGTH);

        if (ARRLENGTH - Flags.tot >= MAXFAIL && netstatus[id] == 1) {
            netstatus[id] = 0;//close
            pthread_mutex_unlock(&switch_mutex);//let SwitchPort runs~
        }
        else if (ARRLENGTH - Flags.tot <= MINFAIL && netstatus[id] == 0) {
            netstatus[id] = 1;//good
            pthread_mutex_unlock(&switch_mutex);//let SwitchPort runs~
        }
        if (flag) wait_recv_data(id, 100000);
        //usleep(500000);
    }

    return 0;
}


void *SwitchPort(void *v) {
    while (true) {
        pthread_mutex_lock(&switch_mutex);

        printf("!!!SwitchPort runs~  Netstatus: ");
        int i;
        for (i = 0; i < 3; i++)
            if (netstatus[i] == 0) printf("Closed.");
            else printf("Good.  ");
        printf("\n");

        int bestport = port_priority[0];
        for (i = 0; i < 3; i++)
            if (netstatus[port_priority[i]]) {
                bestport = port_priority[i];
                break;
            }
        if (bestport != PortNow) ChangeNet(bestport);
    }
}

void ChangeNet(int bestport) {

    pthread_mutex_lock(&tot_mutex);
    PortNow = bestport;
    /*
    Code to change the network
    */
    pthread_mutex_unlock(&tot_mutex);

    printf("!!!!!!!!!!!!!!!!!!!!!Control Port changed to port%d\n", PortNow);
}

void *ForwardUDP(void *v) {
    fd_set inputs;
    char buff[BUFFLENGTH];
    while (true) {
        FD_ZERO(&inputs);
        FD_SET(Socket[3], &inputs);
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, NULL);  //wait until some messages arrive
        if (result <= 0)continue;
        if (FD_ISSET(Socket[3], &inputs)) {
            int msglen = Recvfrom(3, &buff[2]);


            printf("!>>Get UDP from local port<<!\n");

            int PortNow_t = PortNow;
            buff[1] = '3';
            buff[0] = '0' + PortNow_t;
            //printf("%s\n", buff);
            Sendto(PortNow_t, buff, msglen + 2);

        }
    }
    return 0;
}

void init_mutex()//initialize all mutex
{
    pthread_mutex_init(&tot_mutex, NULL);
    pthread_mutex_init(&switch_mutex, NULL);//for SwitchPort()
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

int init_sock_c(int PortId, char c_addr[], char c_port[], char s_addr[], char s_port[]) {

    if ((Socket[PortId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(c_port));
    addr.sin_addr.s_addr = inet_addr(c_addr);

    if (bind(Socket[PortId], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("init_socket_bind");
        exit(1);
    }

    SendAddr[PortId].sin_family = AF_INET;
    SendAddr[PortId].sin_port = htons(atoi(s_port));
    SendAddr[PortId].sin_addr.s_addr = inet_addr(s_addr);
    if (SendAddr[PortId].sin_addr.s_addr == INADDR_NONE) {
        printf("Incorrect ip address!\n");
        close(Socket[PortId]);
        exit(1);
    }
    return 0;
}

int init_sock_s(int PortId, char s_port[]) {

    if ((Socket[PortId] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("init_socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(s_port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(Socket[PortId], (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("init_socket_bind");
        exit(1);
    }
    return 0;
}

int Sendto(int PortId, char msg[], int len) // sendto for client
{
    int n;
    n = (int) sendto(Socket[PortId], msg, len, 0, (struct sockaddr *) &(SendAddr[PortId]), sizeof(SendAddr[PortId]));
    if (n < 0) {
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
    n = (int) recvfrom(Socket[PortId], msg, BUFFLENGTH, MSG_DONTWAIT, (struct sockaddr *) &(RecvAddr[PortId]),
                       (socklen_t *) &addr_len);
    if (n > 0) {
        msg[n] = 0;
    }
    else {
        msg[0] = 0;//clear the string if failed
    }
    return n;
}

int wait_recv(int PortId, char msg[], int WaitTime)// Wait UDP response for WaitTime usec
{
    static char buff[BUFFLENGTH];
    fd_set inputs;
    struct timeval timeout;
    FD_ZERO(&inputs);
    FD_SET(Socket[PortId], &inputs);
    timeout.tv_sec = WaitTime / 1000000;
    timeout.tv_usec = WaitTime % 1000000;
    while (timeout.tv_usec > 0 || timeout.tv_sec > 0) {
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, &timeout);
        //printf("%d : %d : %d\n", result, (int)timeout.tv_sec, (int)timeout.tv_usec);
        if (result <= 0) return 0;
        int n = Recvfrom(PortId, buff);
        if (buff[0] == '3') {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[3].sin_family = RecvAddr[3].sin_family;
            SendAddr[3].sin_port = RecvAddr[3].sin_port;
            SendAddr[3].sin_addr.s_addr = RecvAddr[3].sin_addr.s_addr;
            Sendto(3, &buff[1], n - 1);

            continue;
        }
        if ((n > 0) && (strcmp(msg, buff) == 0))
            return 1;
    }
    return 0;
}

void wait_recv_data(int PortId, int WaitTime)// Wait UDP data package for WaitTime usec
{
    static char buff[BUFFLENGTH];
    fd_set inputs;
    struct timeval timeout;
    FD_ZERO(&inputs);
    FD_SET(Socket[PortId], &inputs);
    timeout.tv_sec = WaitTime / 1000000;
    timeout.tv_usec = WaitTime % 1000000;
    while (timeout.tv_usec > 0 || timeout.tv_sec > 0) {
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, &timeout);
        //printf("%d : %d : %d\n", result, (int)timeout.tv_sec, (int)timeout.tv_usec);
        if (result <= 0) return;
        int n = Recvfrom(PortId, buff);
        if (buff[0] == '3') {
            printf("!!receive UDP data packet from server!: \n");
            SendAddr[3].sin_family = RecvAddr[3].sin_family;
            SendAddr[3].sin_port = RecvAddr[3].sin_port;
            SendAddr[3].sin_addr.s_addr = RecvAddr[3].sin_addr.s_addr;
            Sendto(3, &buff[1], n - 1);
        }
    }
}
