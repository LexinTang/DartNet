#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <signal.h>
#include <pthread.h>

#define MYPORT 47789
#define CONTENT_LENGTH 128
#define BACKLOG 10                  // the queue length of waiting connections

int port;
int sockfd; // listen on sockfd
struct sockaddr_in their_addr; // client's address information
struct sockaddr_in server_addr;
socklen_t sin_size;

void server_init(void);
void server_run(void);
int send_msg_to_client(int sockfd, char *msg);
void shutdown_handler(int);
void *client_thread_fn(void *);

/*
 * Data structure to store client information
 */
struct client {
    int socketfd;                           // the socket to communicate with client
    struct sockaddr_in address;	            // remote client address
    pthread_t client_thread;                // the client thread to receive messages, and then put them in the bounded buffer
};

/*
 * The main server process
 */
int main(int argc, char **argv)
{
    // set port number = 47789
    if (argc > 1) {
        port = atoi(argv[1]);
    } else {
        port = MYPORT;
    }
    
    printf("Starting server ...\n");
    
    // Register "Control + C" signal handler
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);
    
    // Initilize the server
    server_init();
    
    // Run the server
    server_run();
    
    return 0;
}

/*
 * Initilize the server
 */
void server_init(void)
{
    // create a new socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("client socket creation error\n");
        exit(1);
    }
    
    /* Prepare the socket address structure of the server */
    server_addr.sin_family = AF_INET;       // host byte order
    server_addr.sin_port = htons(port);     // short, network byte order
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP address "localhost"
    memset(&(server_addr.sin_zero), '\0', 8); // zero the rest of the struct
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        printf("bind error\n");
        exit(1);
    }
    
    if (listen(sockfd, BACKLOG) == -1) {
        printf("listen error\n");
        exit(1);
    }
    
    printf("Server is up and listening at port %d\n", port);
}

/*
 * Run the chat server
 */
void server_run(void)
{
    while (1) {
        // Listen for new connections
        int new_fd;	//new connection on new_fd
        char mbuf[CONTENT_LENGTH];	//mbuf for received msg
        
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            printf("accept error\n");
            exit(1);
        }
        
        /* receive msg from client */
        memset(&mbuf, 0, sizeof(mbuf));
        if (recv(new_fd, &mbuf, CONTENT_LENGTH-1, 0) == -1) {
            printf("recv error occurs, exit\n");
            exit(1);
        }
        
        int connect_new_fd;
        // create a new socket
        if ((connect_new_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            printf("client socket creation error\n");
            exit(1);
        }
        
        struct sockaddr_in new_server_addr;
        /* Prepare the socket address structure of the server */
        new_server_addr.sin_family = AF_INET;    // host byte order
        new_server_addr.sin_port = htons(0);     // short, network byte order
        new_server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP address
        memset(&(server_addr.sin_zero), '\0', 8); // zero the rest of the struct
        
        if (bind(connect_new_fd, (struct sockaddr *)&new_server_addr, sizeof(struct sockaddr)) == -1) {
            printf("bind error\n");
            exit(1);
        }
        
        if (listen(connect_new_fd, BACKLOG) == -1) {
            printf("listen error\n");
            exit(1);
        }
        
        char * msg = strtok(mbuf, "\n");
        sin_size = sizeof(struct sockaddr_in);
        getsockname(connect_new_fd, (struct sockaddr *)& new_server_addr, &sin_size);
        int new_port = ntohs(new_server_addr.sin_port); // new port number
        
        char reply_msg[CONTENT_LENGTH];
        memset(&reply_msg, 0, sizeof(reply_msg));
        sprintf(reply_msg, "CONNECT localhost %d networks\n", new_port);
        
        if (strcasecmp(msg, "AUTH secretpassword") == 0){
            send_msg_to_client(new_fd, reply_msg);
        }
        else{
            send_msg_to_client(new_fd, "AUTH failed\n");
        }
        
        shutdown(new_fd, SHUT_RDWR);
        close(new_fd);
        
        int talk_fd;
        sin_size = sizeof(struct sockaddr_in);
        if ((talk_fd = accept(connect_new_fd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
            printf("accept error\n");
            exit(1);
        }
        
        memset(&mbuf, 0, sizeof(mbuf));
        if (recv(talk_fd, &mbuf, CONTENT_LENGTH-1, 0) == -1) {
            printf("recv error occurs, exit\n");
            exit(1);
        }
        
        msg = strtok(mbuf, "\n");
        
        if (strcasecmp(msg, "AUTH networks") == 0){
            send_msg_to_client(talk_fd, "SUCCESS\n");
        }
        else{
            send_msg_to_client(talk_fd, "AUTH failed\n");
        }

        memset(&mbuf, 0, sizeof(mbuf));
        if (recv(talk_fd, &mbuf, CONTENT_LENGTH-1, 0) == -1) {
            printf("recv error occurs, exit\n");
            exit(1);
        }
        
        time_t mytime;
        time(&mytime);
        
        srand((unsigned) time(&mytime));
        
        memset(&reply_msg, 0, sizeof(reply_msg));
        
        msg = strtok(mbuf, "\n");
        if (strcasecmp(msg, "WATER TEMPERATURE") == 0){
            sprintf(reply_msg, "%ld %d F\n", mytime, rand() % 300 + 200);
        }
        else if (strcasecmp(msg, "REACTOR TEMPERATURE") == 0){
            sprintf(reply_msg, "%ld %d F\n", mytime, rand() % 200 + 1000);
        }
        else if (strcasecmp(msg, "POWER LEVEL") == 0){
            sprintf(reply_msg, "%ld %d MW\n", mytime, rand() % 70);
        }
        
        send_msg_to_client(talk_fd, reply_msg);
        
        memset(&mbuf, 0, sizeof(mbuf));
        if (recv(talk_fd, &mbuf, CONTENT_LENGTH-1, 0) == -1) {
            printf("recv error occurs, exit\n");
            exit(1);
        }
                               
        msg = strtok(mbuf, "\n");
        if (strcasecmp(msg, "CLOSE") == 0){
            send_msg_to_client(talk_fd, "BYE\n");
        }
        
        shutdown(talk_fd, SHUT_RDWR);
        close(talk_fd);
        shutdown(connect_new_fd, SHUT_RDWR);
        close(connect_new_fd);
        
    }
}

/*
 * Send a message to server
 * Return value:  0 - success;
 *               -1 - error;
 */
int send_msg_to_client(int sockfd, char *msg)
{
    char mbuf[strlen(msg)+1];
    memcpy(mbuf, msg, strlen(msg));
    mbuf[strlen(msg)] = '\0';
    
    if (send(sockfd, &mbuf, sizeof(mbuf), 0) == -1) {
        //perror("Server socket sending error");
        return -1;
    }
    
    return 0;
}

/*
 * Signal handler (when "Ctrl + C" is pressed)
 */
void shutdown_handler(int signum)
{
    printf("Kill by SIGKILL (kill -2)\n");
    printf("Shutdown server .....\n");
    
    //terminates all threads: client_thread(s)
    
    
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    
    printf("Done\n");
    exit(0);
}
