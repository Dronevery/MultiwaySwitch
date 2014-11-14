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
#define ARRLENGTH 10
#define MAXFAIL 3
#define MINFAIL 2

struct netflag
{
    int arr[ARRLENGTH];
    int p,tot;
};
struct param
{
    int id;
};

pthread_mutex_t tot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;    //just for printf when debug
pthread_mutex_t switch_mutex = PTHREAD_MUTEX_INITIALIZER;   //for SwitchPort()
pthread_mutex_t sock_mutex[4] = {//for asock[4]
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER
};


const int port_priority[3] = {0, 1, 2}; //means: port0 has the highest priority to be choosed, than port1,than port2 
int netstatus[3];
unsigned int tot;
struct netflag Flags;
int Socket[4];//sock0,1,2:net to server  sock3:localhost
struct sockaddr_in SendAddr[4];
struct sockaddr_in RecvAddr[4];
int PortNow;

void init_mutex();
int init_netflag(struct netflag *a);
int update_netflag(struct netflag *a, int flag);
int init_sock_c(int PortId, char s_addr[], char s_port[]);
int init_sock_s(int PortId,  char s_port[]);
int Sendto(int PortId, char msg[]);
int Recvfrom(int PortId, char msg[]);
int wait_recv(int PortId, char msg[], int WaitTime);
void * TestPort(void * v);
void * SwitchPort(void * v);
void * ForwardUDP(void * v);
void ChangeNet(int bestport);
struct param * make_param(int id);




int main(int argc, char **argv)
{
    if (argc != 6)
    {
        printf("Usage: %s ip port port port port\nThe 4th port is the local port\n", argv[0]);
        exit(1);
    }
    printf("This is a UDP client\n");

    init_mutex();   //initialize all mutex

    int pi;
    tot = 0;
    PortNow = 0;
    for(pi = 0; pi < 3; pi++)
    {
        netstatus[pi] = 1;
        init_sock_c(pi, argv[1], argv[2+pi]);
    }
    init_sock_s(3, argv[5]);

    pthread_t pt_t[3], pt_s, pt_f;
    pthread_create(&pt_s, NULL, SwitchPort, NULL);
    
    for(pi = 0; pi < 3; pi++)
    {

        struct param * para = make_param(pi);
        pthread_create(&pt_t[pi], NULL, TestPort, (void *)para);
    }
    pthread_create(&pt_f, NULL, ForwardUDP, NULL);

    for(pi = 0; pi < 3; pi++)
    {
        pthread_join(pt_t[pi], NULL);
    }
    return 0;
}


void * TestPort(void * v)
{
    struct param *para = v;
    int id = para->id;
    struct netflag Flags;
    init_netflag(&Flags);

    char msg[BUFFLENGTH];
    while (1)
    {
        pthread_mutex_lock(&tot_mutex);
        tot++;
        sprintf(msg, "%d%u", PortNow, tot);//prepare msg
        pthread_mutex_unlock(&tot_mutex);

        pthread_mutex_lock(&sock_mutex[id]);
        Sendto(id, msg);//send msg
        pthread_mutex_unlock(&sock_mutex[id]);
        //printf("sent: %s\n", msg);

        int flag = wait_recv(id, msg, 1000000);//wait 1s for response

        update_netflag(&Flags, flag);
        pthread_mutex_lock(&print_mutex);
        printf("Port%d %s  Statue: %d/%d \n", id, (flag?("Success."):("Failed. ")), Flags.tot, ARRLENGTH);
        pthread_mutex_unlock(&print_mutex);

        if(ARRLENGTH - Flags.tot >= MAXFAIL && netstatus[id] == 1)
        {
            netstatus[id] = 0;//close
            pthread_mutex_unlock(&switch_mutex);//let SwitchPort runs~
        }
        else if(ARRLENGTH - Flags.tot <= MINFAIL && netstatus[id] == 0)
        {
            netstatus[id] = 1;//good
            pthread_mutex_unlock(&switch_mutex);//let SwitchPort runs~
        } 
        
        usleep(100000);
    }
    
    return 0;
}


void * SwitchPort(void * v)
{
    while(1)
    {
        pthread_mutex_lock(&switch_mutex);
        pthread_mutex_lock(&print_mutex);
        printf("!!!SwitchPort runs~  Netstatus: ");
        int i;
        for(i = 0; i < 3; i++)
            if(netstatus[i] == 0) printf("Closed.");
                else printf("Good.  ");
        printf("\n");
        pthread_mutex_unlock(&print_mutex);

        int bestport = port_priority[2];// assume that the last priority port is most stable
        for(i = 0; i < 3; i++)
        if(netstatus[port_priority[i]])
        {
            bestport = port_priority[i];
            break;
        }
        if(bestport != PortNow) ChangeNet(bestport);
    }
}

void ChangeNet(int bestport)
{

    pthread_mutex_lock(&tot_mutex);
    PortNow = bestport;
    /*
    Code to change the network
    */
    pthread_mutex_unlock(&tot_mutex);

    pthread_mutex_lock(&print_mutex);
    printf("!!!!!!!!!!!!!!!!!!!!!Control Port changed to port%d\n", PortNow);
    pthread_mutex_unlock(&print_mutex);
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

            pthread_mutex_lock(&sock_mutex[PortNow]);
            Sendto(PortNow, buff);
            pthread_mutex_unlock(&sock_mutex[PortNow]);
        }
    }
    return 0;
}

void init_mutex()//initialize all mutex
{
    pthread_mutex_init(&tot_mutex, NULL);
    pthread_mutex_init(&print_mutex, NULL);//just for printf when debug
    pthread_mutex_init(&switch_mutex, NULL);//for SwitchPort()
    int pi;
    for(pi = 0; pi < 4; pi++)
    {
        pthread_mutex_init(&sock_mutex[pi], NULL);
    }
}

struct param * make_param(int id)
{
    struct param *a = malloc(sizeof(struct param));
    a->id = id;
    return a;
}

int init_netflag(struct netflag *a)
{
    int i;
    for(i = 0; i < ARRLENGTH; i++)
    {
        a->arr[i] = 1;
    }
    a->p = 0;
    a->tot = ARRLENGTH;
    return 0;
}

int update_netflag(struct netflag *a, int flag)
{
    a->tot -= a->arr[a->p];
    a->arr[a->p] = flag && 1;
    a->tot += a->arr[a->p];
    a->p = (a->p + 1) % ARRLENGTH;
    return 0;
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

int wait_recv(int PortId, char msg[], int WaitTime)// Wait UDP response for WaitTime usec
{
    static char buff[BUFFLENGTH];
    fd_set inputs;
    struct timeval timeout;
    FD_ZERO(&inputs);
    FD_SET(Socket[PortId], &inputs);
    timeout.tv_sec = WaitTime / 1000000;
    timeout.tv_usec = WaitTime % 1000000;
    while(timeout.tv_usec > 0 || timeout.tv_sec > 0)
    {
        int result = select(FD_SETSIZE, &inputs, NULL, NULL, &timeout);
        //printf("%d : %d : %d\n", result, (int)timeout.tv_sec, (int)timeout.tv_usec);
        if(result <= 0)
            return 0;
        pthread_mutex_lock(&sock_mutex[PortId]);
        int n = Recvfrom(PortId, buff);
        pthread_mutex_unlock(&sock_mutex[PortId]);
        if(buff[0] == '3')
        {
            pthread_mutex_lock(&sock_mutex[3]);
            Sendto(3, &buff[1]);
            pthread_mutex_unlock(&sock_mutex[3]);
            continue;
        }
        if((n > 0) && (strcmp(msg, buff) == 0)) 
            return 1;
    }
    return 0;
}

