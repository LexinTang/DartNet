// FILE: srt_server.c
//
// Description: this file contains server states' definition, some important
// data structures and the server SRT socket interface definitions. You need 
// to implement all these interfaces
//
// Date: April 18, 2008
//       April 21, 2008 **Added more detailed description of prototypes fixed ambiguities** ATC
//       April 26, 2008 **Added GBN descriptions
//

#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include "srt_server.h"

// global varaible TCB table
svr_tcb_t *tcb_table[MAX_TRANSPORT_CONNECTIONS];

// sockect for connection
int svr_conn;

/*several functions help to build SRT socket APIs*/

// This function finds tcb by server port number.
svr_tcb_t *getTCB(unsigned int serverPort){
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] != NULL && tcb_table[i] -> svr_portNum == serverPort){
            return tcb_table[i];
        }
    }
    return NULL;
}

// This function sends segments to client.
// return 1:  ***ACK segment sent successful
//       -1:  ***ACK segment sent failed
int send_ack(svr_tcb_t *tcb, int seg_type){
    seg_t seg_ack;
    memset(&seg_ack, 0, sizeof(seg_ack));
    seg_ack.header.type = seg_type;
    seg_ack.header.src_port = tcb->svr_portNum;  // sent from sever to client
    seg_ack.header.dest_port = tcb->client_portNum;
    seg_ack.header.checksum = 0;
    
    // handle sequence number
    if (seg_type == SYNACK){
        seg_ack.header.length = 0;
    }
    else if (seg_type == FINACK){
        seg_ack.header.length = 0;
    }
    else if (seg_type == DATAACK){
        // handle sequence number and send buffer issues
        seg_ack.header.length = 0;
        seg_ack.header.ack_num = tcb->expect_seqNum;
    }
    
    if (snp_sendseg(svr_conn, &seg_ack) < 0) {
        return -1;
    }
    
    char seg_name[100];
    switch (seg_type){
        case 0:
            strcpy(seg_name, "SYN");
            break;
        case 1:
            strcpy(seg_name, "SYNACK");
            break;
        case 2:
            strcpy(seg_name, "FIN");
            break;
        case 3:
            strcpy(seg_name, "FINACK");
            break;
        case 4:
            strcpy(seg_name, "DATA");
            break;
        case 5:
            sprintf(seg_name, "DATAACK with ack_num %d", seg_ack.header.ack_num);
            //strcpy(seg_name, "DATAACK");
            break;
        default:
            ;
            
    }
    printf("%s sent from %d to %d\n", seg_name, seg_ack.header.src_port, seg_ack.header.dest_port);
    return 1;
}

/*interfaces to application layer*/

//
//
//  SRT socket API for the server side application. 
//  ===================================
//
//  In what follows, we provide the prototype definition for each call and limited pseudo code representation
//  of the function. This is not meant to be comprehensive - more a guideline. 
// 
//  You are free to design the code as you wish.
//
//  NOTE: When designing all functions you should consider all possible states of the FSM using
//  a switch statement (see the Lab3 assignment for an example). Typically, the FSM has to be
// in a certain state determined by the design of the FSM to carry out a certain action. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// This function initializes the TCB table marking all entries NULL. It also initializes 
// a global variable for the overlay TCP socket descriptor ``conn'' used as input parameter
// for snp_sendseg and snp_recvseg. Finally, the function starts the seghandler thread to 
// handle the incoming segments. There is only one seghandler for the server side which
// handles call connections for the client.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void srt_server_init(int conn)
{
    // initialize all entries of TCB table to NULL
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        tcb_table[i] = NULL;
    }
    
    // initialize the connection socket
    svr_conn = conn;
    
    // start the seghandler thread to handle the incoming segments
    pthread_t seghandler_thread;
    pthread_create(&seghandler_thread, NULL, seghandler, NULL);
    
    return;
}


// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the server port set to the function call parameter 
// server port.  The TCB table entry index should be returned as the new socket ID to the server 
// and be used to identify the connection on the server side. If no entry in the TCB table  
// is available the function returns -1.

//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_sock(unsigned int port)
{
    // check if there exits a TCB entry for the current port
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] != NULL && tcb_table[i]->svr_portNum == port){
            return -1;
        }
    }
    
    // find an empty entry in TCB table
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] == NULL){
            // create a new TCB entry and return the index of that entry
            svr_tcb_t *tcb = (svr_tcb_t *)malloc(sizeof(svr_tcb_t));
            tcb->svr_portNum = port;
            tcb->state = CLOSED;
            tcb->expect_seqNum = 0;
            tcb->recvBuf = (char *)malloc(RECEIVE_BUF_SIZE);
            tcb->usedBufLen = 0;
            tcb->bufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
            pthread_mutex_init(tcb->bufMutex, NULL);
            tcb_table[i] = tcb;
            return i;
        }
    }
    
    return -1;
}


// This function gets the TCB pointer using the sockfd and changes the state of the connection to 
// LISTENING. It then starts a timer to ``busy wait'' until the TCB's state changes to CONNECTED 
// (seghandler does this when a SYN is received). It waits in an infinite loop for the state 
// transition before proceeding and to return 1 when the state change happens, dropping out of
// the busy wait loop. You can implement this blocking wait in different ways, if you wish.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_accept(int sockfd)
{
    // get the TCB pointer using the sockfd
    svr_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch (tcb->state){
        case CLOSED:
            // uddate state to LISTENING
            tcb->state = LISTENING;
            // wait for state changing to CONNECTED
            while (1){
                if (tcb->state == CONNECTED){
                    break;
                }
                else{
                    // set a timer for accept polling interval
                    nanosleep((const struct timespec[]){{0, ACCEPT_POLLING_INTERVAL}}, NULL);
                }
            }
            return 1;
        case LISTENING:
            return -1;
        case CONNECTED:
            return -1;
        case CLOSEWAIT:
            return -1;
        default:
            return -1;
    }
}


// Receive data from a srt client. Recall this is a unidirectional transport
// where DATA flows from the client to the server. Signaling/control messages
// such as SYN, SYNACK, etc.flow in both directions. 
// This function keeps polling the receive buffer every RECVBUF_POLLING_INTERVAL
// until the requested data is available, then it stores the data and returns 1
// If the function fails, return -1 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_recv(int sockfd, void* buf, unsigned int length)
{
    // get the TCB pointer using the sockfd
    svr_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch (tcb->state){
        case CLOSED:
            return -1;
        case LISTENING:
            return -1;
        case CONNECTED:
            while (1){
                if (tcb->usedBufLen < length){  // not enought data received
                    sleep(RECVBUF_POLLING_INTERVAL);
                }
                else{
                    // save data into buf
                    pthread_mutex_lock(tcb->bufMutex);
                    memmove(buf, tcb->recvBuf, length);
                    memmove(tcb->recvBuf, &tcb->recvBuf[length], tcb->usedBufLen - length);
                    tcb->usedBufLen -= length;
                    pthread_mutex_unlock(tcb->bufMutex);
                    break;
                }
            }
            return 1;
        case CLOSEWAIT:
            return -1;
        default:
            return -1;
    }
}


// This function calls free() to free the TCB entry. It marks that entry in TCB as NULL
// and returns 1 if succeeded (i.e., was in the right state to complete a close) and -1 
// if fails (i.e., in the wrong state).
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_close(int sockfd)
{
    // find the corresponding tcb entry
    svr_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch(tcb->state){
        case CLOSED:
            // free the TCB entry and set that entry to NULL
            free(tcb->recvBuf);
            free(tcb->bufMutex);
            free(tcb);
            tcb_table[sockfd] = NULL;
            return 1;
        case LISTENING:
            return -1;
        case CONNECTED:
            return -1;
        case CLOSEWAIT:
            return -1;
        default:
            return -1;
    }
}


// This is a thread  started by srt_server_init(). It handles all the incoming 
// segments from the client. The design of seghanlder is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* seghandler(void* arg)
{
    svr_tcb_t *tcb;
    seg_t *segPtr;
    
    // infinite loop calls snp_recvseg()
    while (1){
        // assign enough space for seg_t
        segPtr = (seg_t *)malloc(sizeof(seg_t));
        
        if (snp_recvseg(svr_conn, segPtr) == -1){
            // if recv fails, close overlay connection and terminate thread
            close(svr_conn);
            pthread_detach(pthread_self());
            pthread_exit(0);
        }
        
        // get server TCB from segment received
        tcb = getTCB(segPtr->header.dest_port);
        if (tcb == NULL){
            printf("Not correct server port!\n");
            continue;
        }
        
        // handle server-side FSM
        switch(tcb->state){
            case CLOSED:
                break;
                
            case LISTENING:
                if (segPtr->header.type == SYN){
                    printf("SYN received!\n");
                    
                    // update tcb -> client port
                    tcb->client_portNum = segPtr->header.src_port;
                    
                    // send SYNACK back
                    send_ack(tcb, SYNACK);
                    
                    // update server state to CONNECTED
                    tcb->state = CONNECTED;
                    
                    tcb->expect_seqNum = segPtr->header.seq_num;
                    
                    printf("server CONNECTED!\n");
                }
                else{
                    printf("server listening, no SYN received!\n");
                }
                break;
                
            case CONNECTED:
                // if SYN received
                if (segPtr->header.type == SYN && tcb->client_portNum == segPtr->header.src_port){
                    // send SYNACK back
                    
                    tcb->expect_seqNum = segPtr->header.seq_num;
                    
                    send_ack(tcb, SYNACK);
                    
                    printf("duplicate SYN received!\n");
                }
                // if DATA received
                else if (segPtr->header.type == DATA && tcb->client_portNum == segPtr->header.src_port){
                    printf("DATA with seq_num %d received!\n", segPtr->header.seq_num);
                    // receive data in order, save data into recvBuf
                    if (segPtr->header.seq_num == tcb->expect_seqNum && tcb->usedBufLen + segPtr->header.length <= RECEIVE_BUF_SIZE){
                            pthread_mutex_lock(tcb->bufMutex);
                            memmove(&tcb->recvBuf[tcb->usedBufLen], segPtr->data, segPtr->header.length);
                            tcb->usedBufLen += segPtr->header.length;
                            tcb->expect_seqNum = segPtr->header.seq_num + segPtr->header.length;
                            pthread_mutex_unlock(tcb->bufMutex);
                    }
                    
                    // send DATAACK back
                    send_ack(tcb, DATAACK);
                }
                // if FIN received
                else if (segPtr->header.type == FIN && tcb->client_portNum == segPtr->header.src_port){
                    printf("FIN received!\n");
                    
                    // send FINACK back
                    send_ack(tcb, FINACK);
                    
                    // update server state to CLOSEWAIT
                    tcb->state = CLOSEWAIT;
                    printf("server CLOSEWAIT!\n");
                    
                    // create a new thread for closewait timeout
                    pthread_t closewait_timer_thread;
                    pthread_create(&closewait_timer_thread, NULL, closewait_timer, (void *)tcb);
                    
                }
                break;
                
            case CLOSEWAIT:
                if (segPtr->header.type == FIN && tcb->client_portNum == segPtr->header.src_port){
                    // send FINACK back
                    send_ack(tcb, FINACK);
                    
                    printf("duplicate FIN received!\n");
                }
                break;
            default: ;
        }
        
        // free the assigned space for seg_t
        free(segPtr);
    }
    
    return 0;
}

// This is a thread started by seghandler(). When server gets into the CLOSEWAIT state,
// it started this thread as a timer. Meanwhile, the seghandler thread is still open to
// handle upcoming FIN segments.
void *closewait_timer(void* tcb){
    svr_tcb_t *svr_tcb = (svr_tcb_t *)tcb;
    
    // sleep for CLOSEWAIT_TIME
    sleep(CLOSEWAIT_TIMEOUT);
    
    // update server state to CLOSEWAIT
    svr_tcb->state = CLOSED;
    printf("server CLOSED!\n");
    
    pthread_detach(pthread_self());
    pthread_exit(0);
}