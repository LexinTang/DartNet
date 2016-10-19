//FILE: overlay/overlay.c
//
//Description: this file implements a ON process 
//A ON process first connects to all the neighbors and then starts listen_to_neighbor threads each of which keeps receiving the incoming packets from a neighbor and forwarding the received packets to the SNP process. Then ON process waits for the connection from SNP process. After a SNP process is connected, the ON process keeps receiving sendpkt_arg_t structures from the SNP process and sending the received packets out to the overlay network. 
//
//Date: April 28,2008


#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "overlay.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//you should start the ON processes on all the overlay hosts within this period of time
#define OVERLAY_START_DELAY 60
//#define OVERLAY_START_DELAY 10

/**************************************************************/
//declare global variables
/**************************************************************/

//declare the neighbor table as global variable 
nbr_entry_t* nt; 
//declare the TCP connection to SNP process as global variable
int network_conn; 


/**************************************************************/
//implementation overlay functions
/**************************************************************/

// This thread opens a TCP port on CONNECTION_PORT and waits for the incoming connection from all the neighbors that have a larger node ID than my nodeID,
// After all the incoming connections are established, this thread terminates 
void* waitNbrs(void* arg) {
    //put your code here
    int sockfd;
    // create a new socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror(" socket creation error\n");
        exit(1);
    }
    
    struct sockaddr_in server_addr, client_addr;
    
    /* Prepare the socket address structure of the server */
    server_addr.sin_family = AF_INET;       // host byte order
    server_addr.sin_port = htons(CONNECTION_PORT);   // short, network byte order
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP address "localhost"
    memset(&(server_addr.sin_zero), '\0', 8); // zero the rest of the struct
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind error\n");
        exit(1);
    }
    
    if (listen(sockfd, MAX_NODE_NUM) == -1) {
        perror("listen error\n");
        exit(1);
    }
    
    int conn;
    socklen_t sin_size;
    sin_size = sizeof(struct sockaddr_in);
    
    int nbrNum = topology_getNbrNum();
    int myNodeID = topology_getMyNodeID();
    for (int i = 0; i < nbrNum; i++){
        if (nt[i].nodeID <= myNodeID){
            continue;
        }
        printf("Overlay: waiting for my neighbor!\n");
        if ((conn = accept(sockfd, (struct sockaddr *)&(client_addr), &sin_size)) == -1) {
            perror("accept error\n");
            exit(1);
        }
        nt_addconn(nt, topology_getNodeIDfromip(&(client_addr.sin_addr)), conn);
        printf("Overlay: neighbor node %d has joined!\n", nt[i].nodeID);
    }
    
    // terminate this thread
    pthread_detach(pthread_self());
    pthread_exit(0);
}

// This function connects to all the neighbors that have a smaller node ID than my nodeID
// After all the outgoing connections are established, return 1, otherwise return -1
int connectNbrs() {
    int nbrNum = topology_getNbrNum();
    int myNodeID = topology_getMyNodeID();
    for (int i = 0; i < nbrNum; i++){
        if (nt[i].nodeID >= myNodeID){
            continue;
        }
        
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket creation error\n");
            return -1;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(CONNECTION_PORT);
        server_addr.sin_addr.s_addr = nt[i].nodeIP;
        
        if ((connect(sockfd, (struct sockaddr *)&(server_addr), sizeof(struct sockaddr))) == -1) {
            perror("connect error\n");
            return -1;
        }
        
        nt_addconn(nt, nt[i].nodeID, sockfd);
        printf("Overlay: successfully connected to neighbor node %d!\n", nt[i].nodeID);
    }
    
    return 1;
}

//Each listen_to_neighbor thread keeps receiving packets from a neighbor. It handles the received packets by forwarding the packets to the SNP process.
//all listen_to_neighbor threads are started after all the TCP connections to the neighbors are established 
void* listen_to_neighbor(void* arg) {
    //put your code here
    int *idx = (int *)arg;
    snp_pkt_t* pkt;
    pkt = (snp_pkt_t *)malloc(sizeof(snp_pkt_t));
    
    while (1){
        
        if (recvpkt(pkt, nt[*idx].conn) < 0){
            printf("Overlay: lose coonnection with node %d!\n", nt[*idx].nodeID);
            break;
        }
        printf("Overlay: received a snp_pkt_t packet from node %d!\n", nt[*idx].nodeID);
        if (forwardpktToSNP(pkt, network_conn) > 0){
            printf("Overlay: forward a snp_pkt_t packet to local SNP!\n");
        }
    }
    
    close(nt[*idx].conn);
    nt[*idx].conn = -1;
    free(pkt);
    
    // terminate this thread
    pthread_detach(pthread_self());
    pthread_exit(0);
}

//This function opens a TCP port on OVERLAY_PORT, and waits for the incoming connection from local SNP process. After the local SNP process is connected, this function keeps getting sendpkt_arg_ts from SNP process, and sends the packets to the next hop in the overlay network. If the next hop's nodeID is BROADCAST_NODEID, the packet should be sent to all the neighboring nodes.
void waitNetwork() {
    //put your code here
    int sockfd;
    // create a new socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation error\n");
        exit(1);
    }
    
    struct sockaddr_in server_addr, client_addr;
    /* Prepare the socket address structure of the server */
    server_addr.sin_family = AF_INET;       // host byte order
    server_addr.sin_port = htons(OVERLAY_PORT);   // short, network byte order
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP address "localhost"
    memset(&(server_addr.sin_zero), '\0', 8); // zero the rest of the struct
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind error\n");
        exit(1);
    }
    
    if (listen(sockfd, 1) == -1) {
        perror("listen error\n");
        exit(1);
    }
    
    socklen_t sin_size;
    sin_size = sizeof(struct sockaddr_in);
    
    
    // create connection with local SNP process
    
    while (1){
        if ((network_conn = accept(sockfd, (struct sockaddr *)&(client_addr), &sin_size)) == -1) {
            perror("accept error\n");
            exit(1);
        }
        
        printf("connected to local SNP!\n");
        
        snp_pkt_t *pkt = (snp_pkt_t *)malloc(sizeof(snp_pkt_t));
        int *nextNode = (int *)malloc(sizeof(int));
        int nbrNum = topology_getNbrNum();
        
        while (1){
            // getting packets from SNP process
            
            if (getpktToSend(pkt, nextNode, network_conn) < 0){
                printf("lose connection with local SNP!\n");
                break;
            }
            
            printf("Overlay: get a packet from SNP process! next hop is node %d!\n", *nextNode);
            
            // send packets to the next hop in the overlay network
            if ((*nextNode) == BROADCAST_NODEID){
                for (int i = 0; i < nbrNum; i++){
                    if (nt[i].conn == -1){
                        continue;
                    }
                    sendpkt(pkt, nt[i].conn);
                    printf("Overlay: send a packet to node %d!\n", nt[i].nodeID);
                }
            }
            else {
                for (int i = 0; i < nbrNum; i++){
                    if (nt[i].nodeID == (*nextNode)){
                        sendpkt(pkt, nt[i].conn);
                        printf("Overlay: send a packet to node %d!\n", nt[i].nodeID);
                        break;
                    }
                }
            }
        }
        
        close(network_conn);
        free(pkt);
        free(nextNode);
    }
}

//this function stops the overlay
//it closes all the connections and frees all the dynamically allocated memory
//it is called when receiving a signal SIGINT
void overlay_stop() {
    //put your code here
    close(network_conn);
    nt_destroy(nt);
    printf("overlay is shutting down...\n");
    exit(0);
}

int main() {
	//start overlay initialization
	printf("Overlay: Node %d initializing...\n",topology_getMyNodeID());	

	//create a neighbor table
	nt = nt_create();
	//initialize network_conn to -1, means no SNP process is connected yet
	network_conn = -1;
	
	//register a signal handler which is sued to terminate the process
	signal(SIGINT, overlay_stop);

	//print out all the neighbors
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//start the waitNbrs thread to wait for incoming connections from neighbors with larger node IDs
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//wait for other nodes to start
	sleep(OVERLAY_START_DELAY);
	
	//connect to neighbors with smaller node IDs
	connectNbrs();

	//wait for waitNbrs thread to return
	pthread_join(waitNbrs_thread,NULL);	

	//at this point, all connections to the neighbors are created
	
	//create threads listening to all the neighbors
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay: node initialized...\n");
	printf("Overlay: waiting for connection from SNP process...\n");

	//waiting for connection from  SNP process
	waitNetwork();
}
