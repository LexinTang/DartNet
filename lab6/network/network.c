//FILE: network/network.c
//
//Description: this file implements network layer process  
//
//Date: April 29,2008



#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "network.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//network layer waits this time for establishing the routing paths 
#define NETWORK_WAITTIME 60
//#define NETWORK_WAITTIME 10

/**************************************************************/
//delare global variables
/**************************************************************/
int overlay_conn; 			//connection to the overlay
int transport_conn;			//connection to the transport
nbr_cost_entry_t* nct;			//neighbor cost table
dv_t* dv;				//distance vector table
pthread_mutex_t* dv_mutex;		//dvtable mutex
routingtable_t* routingtable;		//routing table
pthread_mutex_t* routingtable_mutex;	//routingtable mutex


/**************************************************************/
//implementation network layer functions
/**************************************************************/

//This function is used to for the SNP process to connect to the local ON process on port OVERLAY_PORT.
//TCP descriptor is returned if success, otherwise return -1.
int connectToOverlay() {
    //put your code here
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation error\n");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(OVERLAY_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if ((connect(sockfd, (struct sockaddr *)&(server_addr), sizeof(struct sockaddr))) == -1) {
        perror("connect error\n");
        return -1;
    }
    
    return sockfd;
}

//This thread sends out route update packets every ROUTEUPDATE_INTERVAL time
//The route update packet contains this node's distance vector. 
//Broadcasting is done by set the dest_nodeID in packet header as BROADCAST_NODEID
//and use overlay_sendpkt() to send the packet out using BROADCAST_NODEID address.
void* routeupdate_daemon(void* arg) {
    pkt_routeupdate_t *pkt_routeupdate = (pkt_routeupdate_t * )malloc(sizeof(pkt_routeupdate_t));
    snp_pkt_t *pkt = (snp_pkt_t * )malloc(sizeof(snp_pkt_t));
    while (1){
        sleep(ROUTEUPDATE_INTERVAL);
        
        memset(pkt_routeupdate, 0, sizeof(pkt_routeupdate_t));
        // route update packet contains this node's distance vector
        pkt_routeupdate->entryNum = topology_getNodeNum();
        pthread_mutex_lock(dv_mutex);
        for (int i = 0; i < pkt_routeupdate->entryNum; i++){
            pkt_routeupdate->entry[i].nodeID = dv[0].dvEntry[i].nodeID;
            pkt_routeupdate->entry[i].cost = dv[0].dvEntry[i].cost;
        }
        pthread_mutex_unlock(dv_mutex);
        
        memset(pkt, 0, sizeof(snp_pkt_t));
        pkt->header.src_nodeID = topology_getMyNodeID();
        pkt->header.dest_nodeID = BROADCAST_NODEID;
        pkt->header.type = ROUTE_UPDATE;
        pkt->header.length = sizeof(snp_hdr_t) + sizeof(pkt_routeupdate_t);
        memcpy(pkt->data, pkt_routeupdate, sizeof(pkt_routeupdate_t));
        
        if (overlay_sendpkt(BROADCAST_NODEID, pkt, overlay_conn) < 0){
            printf("lose connection with overlay!\n");
            free(pkt_routeupdate);
            free(pkt);
            network_stop();
            break;
        }
        printf("Routing: send a pkt to overlay!\n");
    }
    
    free(pkt_routeupdate);
    free(pkt);
    
    pthread_detach(pthread_self());
    pthread_exit(0);
}

//This thread handles incoming packets from the ON process.
//It receives packets from the ON process by calling overlay_recvpkt().
//If the packet is a SNP packet and the destination node is this node, forward the packet to the SRT process.
//If the packet is a SNP packet and the destination node is not this node, forward the packet to the next hop according to the routing table.
//If this packet is an Route Update packet, update the distance vector table and the routing table. 
void* pkthandler(void* arg) {
    /*
    snp_pkt_t *pkt = (snp_pkt_t *)malloc(sizeof(snp_pkt_t));
    pkt_routeupdate_t *pkt_routeupdate = (pkt_routeupdate_t *)malloc(sizeof(pkt_routeupdate_t));
    seg_t *segPtr = (seg_t *)malloc(sizeof(seg_t));
    */
    snp_pkt_t pkt;
    pkt_routeupdate_t pkt_routeupdate;
    seg_t segPtr;
    
    while(overlay_recvpkt(&pkt, overlay_conn) > 0){
        if (pkt.header.type == ROUTE_UPDATE){
            printf("Routing: received a pkt from neighbor %d!\n",pkt.header.src_nodeID);
            //pkt_routeupdate_t *pkt_routeupdate = (pkt_routeupdate_t *)malloc(sizeof(pkt_routeupdate_t));
            memmove(&pkt_routeupdate, pkt.data, pkt.header.length);
            
            // step 1: update the distance vector table
            pthread_mutex_lock(dv_mutex);
            for (int i = 0; i < pkt_routeupdate.entryNum; i++){
                //printf("jump into dvtable_setcost()\n");
                dvtable_setcost(dv, pkt.header.src_nodeID, pkt_routeupdate.entry[i].nodeID, pkt_routeupdate.entry[i].cost);
            }
            pthread_mutex_unlock(dv_mutex);
            
            // step 2: update the distance vector table and the routing table
            pthread_mutex_lock(dv_mutex);
            pthread_mutex_lock(routingtable_mutex);
            for (int i = 0; i < pkt_routeupdate.entryNum; i++){
                unsigned int new_cost = nbrcosttable_getcost(nct, pkt.header.src_nodeID) +
                pkt_routeupdate.entry[i].cost;
                if (dv[0].dvEntry[i].cost > new_cost){ // find a shortcut, update dv table and routing table
                    dv[0].dvEntry[i].cost = new_cost;
                    routingtable_setnextnode(routingtable, dv[0].dvEntry[i].nodeID, pkt.header.src_nodeID);
                }
            }
            pthread_mutex_unlock(routingtable_mutex);
            pthread_mutex_unlock(dv_mutex);
            
            //printf("going to free pkt_routeupdate\n");
            //free(pkt_routeupdate);
        }
        else if (pkt.header.type == SNP){
            // pkt successfully arrived destination
            printf("SNP: received a packet from neighbor %d!\n",pkt.header.src_nodeID);
            if (topology_getMyNodeID() == pkt.header.dest_nodeID){
                printf("SNP: pkt from %d to %d successfully arrived destination!\n", pkt.header.src_nodeID, pkt.header.dest_nodeID);
                //seg_t *segPtr = (seg_t *)malloc(sizeof(seg_t));
                memmove(&segPtr, pkt.data, pkt.header.length);
                forwardsegToSRT(transport_conn, pkt.header.src_nodeID, &segPtr);
                printf("SNP: forward pkt to SRT!\n");
                continue;
            }
            else{
                pthread_mutex_lock(routingtable_mutex);
                int nextNodeID = routingtable_getnextnode(routingtable, pkt.header.dest_nodeID);
                pthread_mutex_unlock(routingtable_mutex);
                
                overlay_sendpkt(nextNodeID, &pkt, overlay_conn);
                printf("SNP: sent a pkt to nextNode %d through overlay, destination is node %d\n", nextNodeID, pkt.header.dest_nodeID);
                continue;
            }
        }
        else {
            printf("Type not specified in pkt!\n");
        }
    }
    
    printf("lose connection with overlay!\n");
    network_stop();
    
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

//This function stops the SNP process. 
//It closes all the connections and frees all the dynamically allocated memory.
//It is called when the SNP process receives a signal SIGINT.
void network_stop() {
	//put your code here
    close(overlay_conn);
    overlay_conn = -1;
    close(transport_conn);
    transport_conn = -1;
    
    free(dv_mutex);
    free(routingtable_mutex);
    nbrcosttable_destroy(nct);
    dvtable_destroy(dv);
    routingtable_destroy(routingtable);
    
    printf("snp is shutting down...\n");
    exit(0);
}

//This function opens a port on NETWORK_PORT and waits for the TCP connection from local SRT process.
//After the local SRT process is connected, this function keeps receiving sendseg_arg_ts which contains the segments and their destination node addresses from the SRT process. The received segments are then encapsulated into packets (one segment in one packet), and sent to the next hop using overlay_sendpkt. The next hop is retrieved from routing table.
//When a local SRT process is disconnected, this function waits for the next SRT process to connect.
void waitTransport() {
    int sockfd;
    // create a new socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket creation error\n");
        exit(1);
    }
    
    struct sockaddr_in server_addr, client_addr;
    /* Prepare the socket address structure of the server */
    server_addr.sin_family = AF_INET;       // host byte order
    server_addr.sin_port = htons(NETWORK_PORT);   // short, network byte order
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
    
    // wait connection with local SRT process
    while (1){
        if ((transport_conn = accept(sockfd, (struct sockaddr *)&(client_addr), &sin_size)) == -1) {
            perror("accept error\n");
            exit(1);
        }
        
        printf("connected to local SRT!\n");
        
        seg_t *seg = (seg_t *)malloc(sizeof(seg_t));
        int *destNode = (int *)malloc(sizeof(int));
        
        while (1){
            // getting sendseg_arg_ts from SRT process
            if (getsegToSend(transport_conn, destNode, seg) < 0){
                printf("lose connection with local SRT!\n");
                break;
            }
            
            printf("SNP: get a segment from SRT process! Destination is node %d!\n", *destNode);
            
            // retrieve next hop from routing table
            int nextNodeID;
            pthread_mutex_lock(routingtable_mutex);
            nextNodeID = routingtable_getnextnode(routingtable, *destNode);
            pthread_mutex_unlock(routingtable_mutex);
            printf("Next node is %d\n", nextNodeID);
            
            // encapsulate segment into packet
            snp_pkt_t *pkt = (snp_pkt_t *) malloc(sizeof(snp_pkt_t));
            memset(pkt, 0, sizeof(snp_pkt_t));
            pkt->header.dest_nodeID = *destNode;
            pkt->header.src_nodeID = topology_getMyNodeID();
            pkt->header.type = SNP;
            pkt->header.length = sizeof(srt_hdr_t) + seg->header.length;
            memmove(pkt->data, seg, pkt->header.length);
            
            // send packets to the next hop in the overlay network
            overlay_sendpkt(nextNodeID, pkt, overlay_conn);
            printf("SNP: sent a pkt to nextNode %d through overlay_conn %d\n", nextNodeID, overlay_conn);
            
            free(pkt);
        }
        
        close(transport_conn);
        free(seg);
        free(destNode);
    }
}

int main(int argc, char *argv[]) {
	printf("network layer is starting, pls wait...\n");

	//initialize global variables
    //printf("mark 1\n");
	nct = nbrcosttable_create();
    //printf("mark 2\n");
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
    //printf("mark 3\n");
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	overlay_conn = -1;
	transport_conn = -1;
    
    //printf("mark 4\n");
	nbrcosttable_print(nct);
	dvtable_print(dv);
    //printf("mark 5\n");
	routingtable_print(routingtable);

	//register a signal handler which is used to terminate the process
	signal(SIGINT, network_stop);

	//connect to local ON process
    //printf("mark 6\n");
	overlay_conn = connectToOverlay();
	if(overlay_conn<0) {
		printf("can't connect to overlay process\n");
		exit(1);		
	}
	//printf("mark 7\n");
	//start a thread that handles incoming packets from ON process 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//start a route update thread 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("network layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(NETWORK_WAITTIME);
	routingtable_print(routingtable);

	//wait connection from SRT process
	printf("waiting for connection from SRT process\n");
	waitTransport(); 

}


