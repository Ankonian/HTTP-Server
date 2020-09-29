#include <err.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define DEFAULT_POOL_SIZE 4
#define WAIT_TIME_SECONDS 5
int num_entries[100] = {0};
int err_entries[100] = {0};
int server_in_use[100] = {0};
int min_entry_index = 0;
int min_err_index = 0;
int downed_server_count;
int timeout_healthcheck = 0;
bool do_healthcheck = false;
bool all_servers_down = false;
bool timeout_servers_down = false;

char *server_error = (char *)"HTTP/1.1 500 Internal Server Error\r\n\r\n";
pthread_mutex_t q_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t server_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t server_cond = PTHREAD_COND_INITIALIZER;
/* 
=============================================================== 
Modified Linked List queue, adapted from GeeksforGeeks and Jacob Sorber(YouTube)
===============================================================
 */
typedef struct QNode
{
    int *cd;
    struct QNode *next;
} QNode;

QNode *front, *rear = NULL;

void enQueue(int *k)
{
    // Create a new LL node
    QNode *newnode = (QNode *)malloc(sizeof(QNode));
    newnode->cd = k;
    newnode->next = NULL;
    // If queue is empty, then new node is front and rear both
    if (rear == NULL)
    {
        front = newnode;
        rear = newnode;
        return;
    }

    rear->next = newnode;
    rear = newnode;

    // Add the new node at the end of queue and change rear
}
QNode *deQueue()
{
    // If queue is empty, return NULL.
    if (front == NULL)
    {
        return NULL;
    }
    else
    {
        QNode *temp = front;

        front = front->next;

        // If front becomes NULL, then change rear also as NULL
        if (front == NULL)
            rear = NULL;

        return temp;
    }
}

typedef struct thread_package
{
    int tofd;
    int num_servers;
    int thread_id;
    int min_entry;

    int servers[100];
} Thread_package;
typedef struct healthcheck_pkg
{
    int servers[100];
    int num_servers;
} Healthcheck_pkg;
/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport)
{
    int connfd;
    struct sockaddr_in servaddr;

    connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(connectport);

    /* For this assignment the IP address can be fixed */
    inet_pton(AF_INET, "127.0.0.1", &(servaddr.sin_addr));

    if (connect(connfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        return -1;
    return connfd;
}

/*
 * server_listen takes a port number and creates a socket to listen on 
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port)
{
    int listenfd;
    int enable = 1;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        return -1;
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof servaddr) < 0)
        return -1;
    if (listen(listenfd, 500) < 0)
        return -1;
    return listenfd;
}

/*
 * bridge_connections send up to 100 bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd)
{
    char recvline[100];
    int n = recv(fromfd, recvline, 100, 0);
    if (n < 0)
    {
        printf("connection error receiving\n");
        return -1;
    }
    else if (n == 0)
    {
        //printf("receiving connection ended\n");
        return 0;
    }
    recvline[n] = '\0';
    //printf("%s", recvline);
    //sleep(1);
    n = send(tofd, recvline, n, 0);
    if (n < 0)
    {
        printf("connection error sending\n");
        return -1;
    }
    else if (n == 0)
    {
        printf("sending connection ended\n");
        return 0;
    }
    return n;
}

/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void bridge_loop(int sockfd1, int sockfd2)
{
    fd_set set;
    struct timeval timeout;
    int fromfd, tofd;
    while (1)
    {
        // set for select usage must be initialized before each select call
        // set manages which file descriptors are being watched
        FD_ZERO(&set);
        FD_SET(sockfd1, &set);
        FD_SET(sockfd2, &set);

        // same for timeout
        // max time waiting, 5 seconds, 0 microseconds
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        // select return the number of file descriptors ready for reading in set
        switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout))
        {
        case -1:
            printf("error during select, exiting\n");
            return;
        case 0:
            printf("both channels are idle, waiting again\n");
            continue;
        default:
            if (FD_ISSET(sockfd1, &set))
            {
                fromfd = sockfd1;
                tofd = sockfd2;
            }
            else if (FD_ISSET(sockfd2, &set))
            {
                fromfd = sockfd2;
                tofd = sockfd1;
            }
            else
            {
                printf("this should be unreachable\n");
                return;
            }
        }
        if (bridge_connections(fromfd, tofd) <= 0)
        {
            //printf("bridge connection terminated\n");
            return;
        }
    }
}

int find_min(int entries[100], int errors[100])
{
    int min = 10000000;
    int err_min = 100000000;
    int min_index = 0;

    for (int i = 0; i < 100; ++i)
    {
        //printf("entries[%d]: %d\n", i, entries[i]);
        if (entries[i] <= min && entries[i] != 0)
        {
            min = entries[i];
            min_index = i;
            printf("new min in array: %d\n", min);
            if (errors[i] < err_min && entries[i] == min)
            {
                err_min = errors[i];
            }
        }
    }
    printf("min: %d\n", min);
    return min_index;
}

/* 
    Healthcheck thread that will stay idle until countdown timer hits 0, 
    then lock the server mutex to send healthcheck requests, update the num_entries and err_entries array
    unlock and rinse and repeat
 */
void *healthcheck(void *thread_pkg)
{
    Healthcheck_pkg *arg = (Healthcheck_pkg *)thread_pkg;
    int servers[100];
    int num_servers = arg->num_servers;
    memcpy(servers, arg->servers, num_servers * sizeof(int));
    char healthcheck_buffer[100];
    char healthcheck_response[50];

    //char *healthcheck_request = "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\nUser-Agent: curl/7.58.0\r\nAccept: */*\r\n\r\n";
    //printf("healthcheck request: %s\n", healthcheck_request);
    for (int i = 0; i < num_servers; ++i)
    {
        printf("Healthcheck server[%d]: %d\n", i, servers[i]);
    }
    struct timeval timeout;

    while (true)
    {
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        printf("Healthcheck while true\n");
        downed_server_count = 0;
        if ((select(0, NULL, NULL, NULL, &timeout) == 0) || do_healthcheck == true)
        {
            if (do_healthcheck == true)
                printf("\nRth request received, do a healthcheck\n\n");
            printf("2 second timed out in healthcheck\n");
            pthread_mutex_lock(&server_mut);
            timeout_healthcheck = 1;

            printf("locked the list of servers to send healthcheck request\n");
            for (int i = 0; i < num_servers; ++i)
            {
                char curr_entries[100];
                int int_entry;
                char curr_err[100];
                int int_err;

                sprintf(healthcheck_buffer, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\nUser-Agent: curl/7.58.0\r\nAccept: */*\r\n\r\n", servers[i]);

                int h_sock = client_connect(servers[i]);
                if (h_sock < 0)
                {
                    printf("server at port %d is down\n", servers[i]);
                    ++downed_server_count;
                    continue;
                }
                else
                {
                    send(h_sock, healthcheck_buffer, strlen(healthcheck_buffer), 0);
                    while (true)
                    {
                        int healthcheck_reply = recv(h_sock, healthcheck_response, sizeof(healthcheck_response), 0);
                        if (healthcheck_reply == 0)
                        {
                            break;
                        }
                    }

                    sscanf(healthcheck_response, "%*s %*s %*s %*s %*s %s", curr_err);
                    sscanf(healthcheck_response, "%*s %*s %*s %*s %*s %*s %s", curr_entries);

                    sscanf(curr_entries, "%d", &int_entry);
                    sscanf(curr_err, "%d", &int_err);

                    num_entries[i] = int_entry;
                    err_entries[i] = int_err;
                    pthread_cond_signal(&server_cond);
                }
            }
            if (downed_server_count == num_servers)
            {
                printf("all servers are down after timeout\n");
                all_servers_down = true;
                timeout_servers_down = true;
            }
            else
            {
                printf("downed server != num_servers, signaling");
            }

            // for (int i = 0; i < 100; ++i)
            // {
            //     printf("num_entries[%d]: %d\n", i, num_entries[i]);
            // }
            do_healthcheck = false;
            pthread_mutex_unlock(&server_mut);
        }
    }

    return NULL;
}
/* 
    Thread function that will function as a dispatcher that will dequeue socket number from queue and pass to each thread
 */
void *dispatch(void *q)
{
    printf("reached dispatch\n");
    Thread_package *arg = (Thread_package *)q;
    int serverport;
    int serverports[100];
    int num_servers = arg->num_servers;
    printf("num_servers in dispatch: %d\n", num_servers);
    memcpy(serverports, arg->servers, num_servers * sizeof(int));
    int connfd;

    while (true)
    {
        //size_t num_servers = sizeof(serverports) / sizeof(int);
        printf("num servers: %d\n", num_servers);

        int choose_server = 0;
        // while (true)
        // {
        pthread_mutex_lock(&server_mut);
        printf("choosing a server\n");

        pthread_cond_wait(&server_cond, &server_mut);
        // while (timeout_healthcheck == 1)
        // {
        //     pthread_mutex_unlock(&server_mut);
        //     pthread_mutex_lock(&server_mut);
        // }
        printf("Got a signal not all servers are down\n");
        int min_load_index = find_min(num_entries, err_entries);
        serverport = serverports[min_load_index];
        printf("server port chosen: %d with load: %d\n", serverport, num_entries[min_load_index]);
        ++num_entries[min_load_index];
        pthread_mutex_unlock(&server_mut);

        printf("serverport: %d\n", serverport);
        //conditional wait for this thread's turn to dequeue
        printf("connecting\n");
        if ((connfd = client_connect(serverport)) < 0)
        {
            err(1, "failed connecting");
        }

        pthread_mutex_lock(&q_mut);
        QNode *pclient = deQueue();
        if (pclient == NULL)
        {
            pthread_cond_wait(&q_cond, &q_mut);
            pclient = deQueue();
        }
        // pthread_cond_wait(&q_cond, &q_mut);
        // pclient = deQueue();
        pthread_mutex_unlock(&q_mut);

        if (pclient != NULL)
        {
            int *acceptsocket = pclient->cd;
            int acceptfd = *acceptsocket;
            printf("==========\nDEQUEUED: %d\n==========\n", acceptfd);
            bridge_loop(acceptfd, connfd);
        }
        pthread_mutex_lock(&server_mut);
        server_in_use[choose_server] = 0;
        pthread_mutex_unlock(&server_mut);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    ssize_t num_threads = DEFAULT_POOL_SIZE;
    int listenfd, acceptfd;
    uint16_t clientport;
    int opt;
    int server_ports[100];
    bool first_arg = true;
    int req_healthcheck = 0;
    char healthcheck_buffer[100];
    char healthcheck_response[50];
    if (argc < 3)
    {
        printf("missing arguments: usage %s port_to_connect port_to_listen", argv[0]);
        return 1;
    }

    //processing flags
    while ((opt = getopt(argc, argv, "N:R:")) != -1)
    {
        switch (opt)
        {
        case 'N':
            if (optarg == NULL)
            {
                num_threads = DEFAULT_POOL_SIZE;
            }
            else
            {
                num_threads = atoi(optarg);
            }

            break;
        case 'R':

            /* code */
            if (optarg == NULL)
            {
                err(-1, "-R requires integer argument\n");
            }
            else
            {
                req_healthcheck = atoi(optarg);
                printf("Healthcheck per %d requests\n", req_healthcheck);
            }

            break;
        }
    }
    //processing port numbers
    int num_servers = 0;
    for (; optind < argc; optind++)
    {
        if (first_arg)
        {
            first_arg = false;
            clientport = atoi(argv[optind]);
        }
        else
        {
            //printf("argv[%d]: %s\n", optind, argv[optind]);
            server_ports[num_servers] = atoi(argv[optind]);
            //printf("server ports[%d]: %d\n", num_servers, server_ports[num_servers]);
            ++num_servers;
        }
    }

    pthread_mutex_lock(&server_mut);
    printf("locked the list of servers to send healthcheck request\n");
    for (int i = 0; i < num_servers; ++i)
    {
        char curr_entries[100];
        int int_entry;
        char curr_err[100];
        int int_err;

        sprintf(healthcheck_buffer, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\nUser-Agent: curl/7.58.0\r\nAccept: */*\r\n\r\n", server_ports[i]);

        int h_sock = client_connect(server_ports[i]);
        if (h_sock < 0)
        {
            printf("server at port %d is down\n", server_ports[i]);
            ++downed_server_count;
            continue;
        }
        else
        {
            /* code */
            send(h_sock, healthcheck_buffer, strlen(healthcheck_buffer), 0);
            recv(h_sock, healthcheck_response, sizeof(healthcheck_response), 0);
            all_servers_down = false;
            sscanf(healthcheck_response, "%*s %*s %*s %*s %*s %s", curr_err);
            sscanf(healthcheck_response, "%*s %*s %*s %*s %*s %*s %s", curr_entries);

            sscanf(curr_entries, "%d", &int_entry);
            sscanf(curr_err, "%d", &int_err);

            num_entries[i] = int_entry;
            err_entries[i] = int_err;
            pthread_cond_signal(&server_cond);
        }
    }
    if (downed_server_count == num_servers)
    {
        printf("all servers are down initially\n");
        all_servers_down = true;
        timeout_servers_down = true;
    }
    else
    {
        printf("Servers are not down initially, signaling\n");
    }
    pthread_mutex_unlock(&server_mut);

    pthread_t thread_pool[num_threads];
    pthread_t healthcheck_thread;
    Healthcheck_pkg h_arg;
    h_arg.num_servers = num_servers;
    memcpy(h_arg.servers, server_ports, num_servers * sizeof(int));
    pthread_create(&healthcheck_thread, NULL, healthcheck, &h_arg);

    //connectport = atoi(argv[1]);
    //printf("num servers in main: %d\n", num_servers);
    Thread_package args[num_threads];

    //printf("num threads in main: %ld\n", num_threads);
    for (int i = 0; i < num_threads; ++i)
    {
        memcpy(args[i].servers, server_ports, num_servers * sizeof(int));
        for (int j = 0; j < num_servers; ++j)
        {
            printf("packing this server to threads: %d\n", args[i].servers[j]);
        }
        args[i].num_servers = num_servers;
        args[i].thread_id = i;
        //printf("created thread #%d\n", i);
        pthread_create(&thread_pool[i], NULL, dispatch, &args[i]);
    }

    //pthread_create(&thread_pool[0], NULL, dispatch, &args);

    // Remember to validate return values
    // You can fail tests for not validating

    //listening for client
    printf("listening\n");
    if ((listenfd = server_listen(clientport)) < 0)
        err(1, "failed listening");

    int request_count = 0;
    while (true)
    {
        printf("accepting\n");
        if ((acceptfd = accept(listenfd, NULL, NULL)) < 0)
            err(1, "failed accepting");
        if ((timeout_servers_down == true) || (all_servers_down == true))
        {
            printf("all servers are down after healthcheck timeout, send server error message\n");
            int size_sent = send(acceptfd, server_error, strlen(server_error), 0);
            close(acceptfd);
            printf("bytes of response sent: %d\n", size_sent);
        }

        ++request_count;
        if (req_healthcheck > 0)
        {
            printf("use -R to check request count\n");
            if ((request_count % req_healthcheck == 0))
            {
                pthread_mutex_lock(&server_mut);
                do_healthcheck = true;
                pthread_mutex_unlock(&server_mut);
            }
        }
        int *new_cd = malloc(sizeof(int));
        *new_cd = acceptfd;
        pthread_mutex_lock(&q_mut);
        enQueue(new_cd);
        printf("==========\nENQUEUED: %d\n==========\n", acceptfd);
        pthread_cond_signal(&q_cond);
        pthread_mutex_unlock(&q_mut);
    }
}
