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

#define CONTENT_LENGTH 128

/*
 * Send a message to server
 * Return value:  0 - success;
 *               -1 - error;
 */
int send_msg_to_server(int sockfd, char *msg)
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
 * Connect the server and get response from it
 * Return value:  0 - success;
 *               -1,-2 - error;
 */
int join_server(int sockfd, struct sockaddr_in server_addr, int user_command)
{
    int new_port;
    
    // make a connection to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        return -1;
	}
    
    // client sends message "AUTH secretpassword\n" to the server
    if (send_msg_to_server(sockfd, "AUTH secretpassword\n") == -1){
        return -1;
    }
    
    char mbuf[CONTENT_LENGTH];
    // get the response from the server
    if (recv(sockfd, &mbuf, sizeof(mbuf), 0) == -1) {
        printf("socket receive error (%s)", strerror(errno));
        return -1;
	}
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    
    // extract new server address from the response
    strtok(mbuf, " ");
    char *get_server_name = strtok(NULL, " ");
    char *port_msg = strtok(NULL, " ");
//    const char *password = strtok(NULL, "\0");
    new_port = atoi(port_msg);
    
    // create e a new socket
    int sockfd2;
    if ((sockfd2 = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("client socket creation error\n");
    }
    
    struct hostent *remote_host;        // the remote host identity
    char server_name[CONTENT_LENGTH];
    strcpy(server_name, get_server_name);
    
    if ((remote_host = gethostbyname(server_name)) == NULL) {
        printf("Cannot resolve the remote host name, %s", server_name);
    }
    
    struct sockaddr_in new_server_addr;
    memset(&new_server_addr, 0, sizeof(new_server_addr));
    new_server_addr.sin_family = AF_INET;
    //new_server_addr.sin_addr = *((struct in_addr *)remote_host->h_addr);
    //new_server_addr.sin_addr.s_addr = inet_addr("129.170.213.101");
    new_server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    new_server_addr.sin_port = htons(new_port);
    
    // make a new connection
    if (connect(sockfd2, (struct sockaddr *)&new_server_addr, sizeof(struct sockaddr)) == -1) {
        printf("connect: Connection refused\n\n");
        shutdown(sockfd2, SHUT_RDWR);
        close(sockfd2);
        return -2;
    }
    
    // Authentication through password
    send_msg_to_server(sockfd2, "AUTH networks\n");
    
    // get response from the server
    memset(&mbuf, 0, sizeof(mbuf));
    if (recv(sockfd2, &mbuf, sizeof(mbuf), 0) == -1) {
        printf("socket receive error (%s)", strerror(errno));
    }
    
    char *type = "WATER TEMPERATURE";
    if (user_command == 1){
        send_msg_to_server(sockfd2, "WATER TEMPERATURE\n");
        type = "WATER TEMPERATURE";
    }
    else if (user_command == 2){
        send_msg_to_server(sockfd2, "REACTOR TEMPERATURE\n");
        type = "REACTOR TEMPERATURE";
    }
    else if (user_command == 3){
        send_msg_to_server(sockfd2, "POWER LEVEL\n");
        type = "POWER LEVEL";
    }

    // get response from the server
    memset(&mbuf, 0, sizeof(mbuf));
    if (recv(sockfd2, &mbuf, sizeof(mbuf), 0) == -1) {
        printf("socket receive error (%s)", strerror(errno));
    }
    
    time_t mytime;
    time(&mytime);

    strtok(mbuf, " ");
    char *num = strtok(NULL, " ");
    char *unit = strtok(NULL, "\0");
    char time_msg[CONTENT_LENGTH];
    strcpy(time_msg, ctime(&mytime));
    time_msg[strlen(time_msg) - 1] = '\0';
    printf("\nThe last %s was taken %s and was %s %s \n", type, time_msg, num, unit);
    
    // close connection
    send_msg_to_server(sockfd2, "CLOSE\n");
    shutdown(sockfd2, SHUT_RDWR);
    close(sockfd2);
    return 0;
}

 /*
 * Basic fuction to test input error
 * Return value:  0 - success;
 *               -1 - error;
 */
int test_input_error(int user_command)
{
    int ret = 0;
    //Check validity of user_command
    if (user_command == 1 || user_command == 2 || user_command == 3){
        ret = 0;
    }
    else{
        ret = -1;
    }
    return ret;
}

/*
 * The main client function
 */
int main(int argc, char *argv[])
{
    printf("WELCOME TO THE THREE MILE ISLAND SENSOR NETWORK\n\n\n");
    int user_command;
    int port;
    
    // loop continuously until force quit
    while (1) {
        printf("Which sensor would you like to read:\n\n");
        printf("\t(1) Water temperature\n\t(2) Reactor temperature\n\t(3) Power level\n\n");
        printf("Selection: \n");
        
        scanf("%d", &user_command);
        
        if (test_input_error(user_command) != 0) {
            printf("\n*** Invalid selection.\n");
            continue;
        }
        
        /****** get the server info **************************/
        struct sockaddr_in server_addr;  // remote host internet address
        char *default_port = "47789";
        port = atoi(default_port);
        
        int sockfd;  // the socket file descriptor
        // create e a new socket
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            printf("client socket creation error\n");
            goto END;
        }
        
        // initialize the remote host internet address
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        //server_addr.sin_addr.s_addr = inet_addr("129.170.213.101");
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        /*****************************************************/
        
        // connect to the server and get response from it
        if (join_server(sockfd, server_addr, user_command) == -1) {
            printf("connect: Connection refused\n\n");
            shutdown(sockfd, SHUT_RDWR);
            close(sockfd);
        }
    }
END:
    return 0;
} 
