//
// FILE: srt_client.c
//
// Description: this file contains client states' definition, some important data structures
// and the client SRT socket interface definitions. You need to implement all these interfaces
//
// Date: April 18, 2008
//       April 21, 2008 **Added more detailed description of prototypes fixed ambiguities** ATC
//       April 26, 2008 ** Added GBN and send buffer function descriptions **
//
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "srt_client.h"

// global varaible TCB table
client_tcb_t *tcb_table[MAX_TRANSPORT_CONNECTIONS];

// sockect for connection
int client_conn;

/*several functions help to build SRT socket APIs*/

// This function finds tcb by client port number.
client_tcb_t *getTCB(unsigned int clientPort){
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] != NULL && tcb_table[i] -> client_portNum == clientPort){
            return tcb_table[i];
        }
    }
    return NULL;
}

// This function sends segments to server.
// return 1:  Segment sent successful
//       -1:  Segment sent failed
int send_seg(client_tcb_t *tcb, int seg_type, segBuf_t *segBuf){
    seg_t seg;
    memset(&seg, 0, sizeof(seg));
    seg.header.type = seg_type;
    seg.header.src_port = tcb->client_portNum;  // sent from client to server
    seg.header.dest_port = tcb->svr_portNum;
    seg.header.checksum = 0;
    
    // handle sequence number
    if (seg_type == SYN){
        seg.header.length = 0;
        seg.header.seq_num = 0;
    }
    else if (seg_type == FIN){
        seg.header.length = 0;
    }
    else if (seg_type == DATA){
        // handle sequence number and send buffer issues
        seg.header.length = segBuf->seg.header.length;
        seg.header.seq_num = segBuf->seg.header.seq_num;
        memmove(seg.data, segBuf->seg.data, seg.header.length);
    }
    
    if (snp_sendseg(client_conn, &seg) < 0) {
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
            sprintf(seg_name, "DATA with seq_num %d", seg.header.seq_num);
            //strcpy(seg_name, "DATA");
            break;
        case 5:
            strcpy(seg_name, "DATAACK");
            break;
        default:
            ;
            
    }
    printf("%s sent from %d to %d\n", seg_name, seg.header.src_port, seg.header.dest_port);
    return 1;
}

/*interfaces to application layer*/

//
//
//  SRT socket API for the client side application. 
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
// handle the incoming segments. There is only one seghandler for the client side which
// handles call connections for the client.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void srt_client_init(int conn)
{
    // initialize all entries of TCB table to NULL
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        tcb_table[i] = NULL;
    }
    
    // initialize the connection socket
    client_conn = conn;
    
    // start the seghandler thread to handle the incoming segments
    pthread_t seghandler_thread;
    pthread_create(&seghandler_thread, NULL, seghandler, NULL);
    
    return;
}


// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the client port set to the function call parameter 
// client port.  The TCB table entry index should be returned as the new socket ID to the client 
// and be used to identify the connection on the client side. If no entry in the TC table  
// is available the function returns -1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_sock(unsigned int client_port)
{
    // check if there exits a TCB entry for the current port
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] != NULL && tcb_table[i]->client_portNum == client_port){
            return -1;
        }
    }
    
    // find an empty entry in TCB table
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] == NULL){
            // create a new TCB entry and return the index of that entry
            client_tcb_t *tcb = (client_tcb_t *)malloc(sizeof(client_tcb_t));
            tcb->client_portNum = client_port;
            tcb->state = CLOSED;
            tcb->next_seqNum = 0;
            tcb->sendBufHead = NULL;
            tcb->sendBufunSent = NULL;
            tcb->sendBufTail = NULL;
            tcb->unAck_segNum = 0;
            tcb->bufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
            pthread_mutex_init(tcb->bufMutex, NULL);
            tcb_table[i] = tcb;
            return i;
        }
    }
    
    return -1;
}


// This function is used to connect to the server. It takes the socket ID and the 
// server's port number as input parameters. The socket ID is used to find the TCB entry.  
// This function sets up the TCB's server port number and a SYN segment to send to
// the server using snp_sendseg(). After the SYN segment is sent, a timer is started. 
// If no SYNACK is received after SYNSEG_TIMEOUT timeout, then the SYN is 
// retransmitted. If SYNACK is received, return 1. Otherwise, if the number of SYNs 
// sent > SYN_MAX_RETRY,  transition to CLOSED state and return -1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_connect(int sockfd, unsigned int server_port)
{
    client_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch(tcb->state) {
        case CLOSED:
            // sets up the TCB’s server port number
            tcb->svr_portNum = server_port;
            
            // send a SYN segment to the server
            send_seg(tcb, SYN, NULL);
            
            // update state to SYNSENT
            tcb->state = SYNSENT;
            
            // set a counter for the number of retransmissions
            int retrans_counter = 0;
            while (retrans_counter < SYN_MAX_RETRY){
                // set a timer for SYNSEG_TIMEOUT
                nanosleep((const struct timespec[]){{0, SYN_TIMEOUT}}, NULL);
                
                if (tcb->state == CONNECTED){   //SYNACK is received during SYNSENT
                    return 1;
                }
                else{ // no SYNACK received after SYNSEG_TIMEOUT timeout
                    // resend a SYN segment to the server
                    send_seg(tcb, SYN, NULL);
                    printf("SYN resent!\n");
                    retrans_counter++;
                }
            }
            
            // reach SYN retry limit, update state to CLOSED
            tcb->state = CLOSED;
            printf("client CLOSED!\n");
            return -1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            return -1;
        case FINWAIT:
            return -1;
        default:
            return -1;
    }
}


// This thread continuously polls send buffer to trigger timeout events
// It should always be running when the send buffer is not empty
// If the current time - first sent-but-unAcked segment's sent time > DATA_TIMEOUT, a timeout event occurs
// When timeout, resend all sent-but-unAcked segments
// When the send buffer is empty, this thread terminates
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void* sendBuf_timer(void* clienttcb)
{
    client_tcb_t *tcb = (client_tcb_t *)clienttcb;
    while (1){
        // set a timer for SENDBUF_POLLING_INTERVAL, wait a shord period for unAck_segNum to change
        sleep(1);
        
        if (tcb->unAck_segNum == 0){
            break;
        }
        
        struct timeval time;
        gettimeofday(&time, NULL);
        //printf("current time is: %ld\n", time.tv_sec * 1000 + time.tv_usec / 1000);
        if (tcb->sendBufHead->sentTime > 0 && time.tv_sec * 1000 + time.tv_usec / 1000 - tcb->sendBufHead->sentTime > DATA_TIMEOUT){
            printf("ready to resend!\n");
            // resend all sent-but-unAcked segments
            pthread_mutex_lock(tcb->bufMutex);
            
            segBuf_t *head = tcb->sendBufHead;
            while (head != tcb->sendBufunSent){
                // modify send time of segBuf(in ms)
                struct timeval time;
                gettimeofday(&time, NULL);
                head->sentTime = time.tv_sec * 1000 + time.tv_usec / 1000;
                
                send_seg(tcb, DATA, head);
                //printf("resend DATA with seq_num %d from %d to %d\n", head->seg.header.seq_num, tcb->client_portNum, tcb->svr_portNum);
                head = head->next;
            }
            
            pthread_mutex_unlock(tcb->bufMutex);
        }
        
    }
    
    pthread_detach(pthread_self());
    pthread_exit(0);
}

// this function is used to send "unsent" segBufs until the number of sent-but-not-Acked segments reaches GBN_WINDOW.
int send_sendBuf(client_tcb_t *tcb)
{
    pthread_mutex_lock(tcb->bufMutex);
    while (tcb->sendBufunSent != NULL && tcb->unAck_segNum < GBN_WINDOW){
        // start the sendbuf_timer thread if linked list is empty
        if (tcb->unAck_segNum == 0){
            pthread_t sendBuf_timer_thread;
            pthread_create(&sendBuf_timer_thread, NULL, sendBuf_timer, (void *)tcb);
            //printf("send_sendBuf thread created!\n");
        }

        struct timeval time;
        gettimeofday(&time, NULL);
        tcb->sendBufunSent->sentTime = (time.tv_sec * 1000 + time.tv_usec / 1000);
        if (send_seg(tcb, DATA, tcb->sendBufunSent) < 0){
            return -1;
        }
        tcb->sendBufunSent = tcb->sendBufunSent->next;
        tcb->unAck_segNum++;
    }
    pthread_mutex_unlock(tcb->bufMutex);
    return 1;
}

// Send data to a srt server. This function should use the socket ID to find the TCP entry.
// Then It should create segBufs using the given data and append them to send buffer linked list. 
// If the send buffer was empty before insertion, a thread called sendbuf_timer 
// should be started to poll the send buffer every SENDBUF_POLLING_INTERVAL time
// to check if a timeout event should occur. If the function completes successfully, 
// it returns 1. Otherwise, it returns -1.
// 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_send(int sockfd, void* data, unsigned int length)
{
    client_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    segBuf_t *segBuf;
    int seg_count;
    int remainder_len;
    switch(tcb->state) {
        case CLOSED:
            return -1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            // cut data into several segments if data is too large
            seg_count = length / MAX_SEG_LEN;
            remainder_len = length % MAX_SEG_LEN;
            if (remainder_len > 0){
                seg_count++;
            }
            
            // create new segBufs using data and append them to send buffer linked list
            for (int i = 0; i < seg_count; i++){
                
                // create new segBufs using data
                segBuf = (segBuf_t *)malloc(sizeof(segBuf_t));
                segBuf->seg.header.length = (remainder_len > 0 && i == seg_count - 1)? remainder_len:MAX_SEG_LEN;
                char *dataPtr = (char *)data;
                memmove(segBuf->seg.data, &dataPtr[i * MAX_SEG_LEN], segBuf->seg.header.length);
                segBuf->seg.header.seq_num = tcb->next_seqNum;
                tcb->next_seqNum += segBuf->seg.header.length;
                segBuf->sentTime = 0;
                segBuf->next = NULL;
                
                // append these new created segBufs to send buffer linked list
                pthread_mutex_lock(tcb->bufMutex);
                if (tcb->sendBufHead == NULL){
                    tcb->sendBufHead= segBuf;
                    tcb->sendBufunSent = segBuf;
                    tcb->sendBufTail = segBuf;
                }
                else{
                    tcb->sendBufTail->next = segBuf;
                    tcb->sendBufTail = segBuf;
                    if (tcb->sendBufunSent == NULL){
                        tcb->sendBufunSent = segBuf;
                    }
                }
                pthread_mutex_unlock(tcb->bufMutex);
            }
            
            // send segBufs until the number of sent-but-not-Acked segments reaches GBN_WINDOW.
            if (send_sendBuf(tcb) < 0){
                return -1;
            }
            return 1;
        case FINWAIT:
            return -1;
        default:
            return -1;
    }
}

// This function is used to clear send buffer linked list
int clear_send_buffer(client_tcb_t *tcb) {
    pthread_mutex_lock(tcb->bufMutex);
    segBuf_t *head = tcb->sendBufHead;
    while(head != NULL) {
        segBuf_t *temp = head;
        head = head->next;
        free(temp);
    }
    
    tcb->sendBufHead = NULL;
    tcb->sendBufTail = NULL;
    tcb->sendBufunSent = NULL;
    tcb->unAck_segNum = 0;
    pthread_mutex_unlock(tcb->bufMutex);
    return 1;
}

// This function is used to disconnect from the server. It takes the socket ID as
// an input parameter. The socket ID is used to find the TCB entry in the TCB table.
// This function sends a FIN segment to the server. After the FIN segment is sent
// the state should transition to FINWAIT and a timer started. If the 
// state == CLOSED after the timeout the FINACK was successfully received. Else,
// if after a number of retries FIN_MAX_RETRY the state is still FINWAIT then
// the state transitions to CLOSED and -1 is returned.


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_client_disconnect(int sockfd)
{
    client_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch(tcb->state) {
        case CLOSED:
            return -1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            // send a FIN segment to the server
            send_seg(tcb, FIN ,NULL);
            
            // update state to FINWAIT
            tcb->state = FINWAIT;
            printf("client in FINWAIT\n");
            
            // set a counter for the number of retransmissions
            int retrans_counter = 0;
            while (retrans_counter < FIN_MAX_RETRY){
                // set a timer for FINSEG_TIMEOUT
                nanosleep((const struct timespec[]){{0, FIN_TIMEOUT}}, NULL);
                
                if (tcb->state == CLOSED){   //FINACK is received during FINWAIT
                    tcb->svr_portNum = 0;
                    tcb->next_seqNum = 0;
                    // clear send buffer linked list
                    clear_send_buffer(tcb);
                    return 1;
                }
                else{ // no FINACK received after FINSEG_TIMEOUT_NS timeout
                    // send a FIN segment to the server
                    send_seg(tcb, FIN, NULL);
                    printf("FIN resent!\n");
                    retrans_counter++;
                }
            }
            
            // reach FIN retry limit, update state to CLOSED
            tcb->state = CLOSED;
            return -1;
        case FINWAIT:
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
int srt_client_close(int sockfd)
{
    // find the corresponding tcb entry
    client_tcb_t *tcb = tcb_table[sockfd];
    if (tcb == NULL){
        return -1;
    }
    
    switch(tcb->state){
        case CLOSED:
            // free the TCB entry and set that entry to NULL
            free(tcb->bufMutex);
            free(tcb);
            tcb_table[sockfd] = NULL;
            return 1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            return -1;
        case FINWAIT:
            return -1;
        default:
            return -1;
    }
}


// This is a thread  started by srt_client_init(). It handles all the incoming 
// segments from the server. The design of seghanlder is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void *seghandler(void* arg)
{
    client_tcb_t *tcb;
    seg_t *segPtr;
    
    // infinite loop calls snp_recvseg()
    while (1){
        // assign enough space for seg_t
        segPtr = (seg_t *)malloc(sizeof(seg_t));
        
        if (snp_recvseg(client_conn, segPtr) == -1){
            // if recv fails, close overlay connection and terminate thread
            close(client_conn);
            pthread_detach(pthread_self());
            pthread_exit(0);
        }
        
        // get client TCB from segment received
        tcb = getTCB(segPtr->header.dest_port);
        if (tcb == NULL){
            printf("Not correct client port!\n");
            continue;
        }
        
        // handle client-side FSM
        switch (tcb->state){
            case CLOSED:
                break;
                
            case SYNSENT:
                if (segPtr->header.type == SYNACK && segPtr->header.src_port == tcb->svr_portNum){
                    printf("SYNACK received!\n");
                    
                    // update client state to CONNECTED
                    tcb->state = CONNECTED;
                    printf("client CONNECTED!\n");
                }
                else {
                    printf("client in SYNSENT, no SYNACK received!\n");
                }
                break;
                
            case CONNECTED:
                if (segPtr->header.type == DATAACK && segPtr->header.src_port == tcb->svr_portNum){
                    printf("DATAACK with ack_num %d received!\n", segPtr->header.ack_num);
                    
                    // free all the segments with a smaller sequence number than ack_number
                    pthread_mutex_lock(tcb->bufMutex);
                    segBuf_t *head = tcb->sendBufHead;
                    while(head != tcb->sendBufunSent && head->seg.header.seq_num < segPtr->header.ack_num){
                        segBuf_t *temp = head;
                        head = head -> next;
                        free(temp);
                        tcb->sendBufHead = head;
                        tcb->unAck_segNum--;
                    }
                    
                    if (head == NULL){
                        tcb->sendBufTail = NULL;
                    }
                    pthread_mutex_unlock(tcb->bufMutex);
                    
                    // GBN window opens up, so send segments in send buffer
                    send_sendBuf(tcb);
                }
                else {
                    printf("client in CONNECTED, no DATAACK received!\n");
                }
                break;
                
            case FINWAIT:
                if (segPtr->header.type == FINACK && segPtr->header.src_port == tcb->svr_portNum){
                    printf("FINACK received!\n");
                    
                    // update client state to CLOSED
                    tcb->state = CLOSED;
                    printf("client CLOSED!\n");
                }
                else {
                    printf("client in FINWAIT, no FINACK received!\n");
                }
                break;
            default:
                ;
        }
        // free the assigned space for seg_t
        free(segPtr);
    }
    
    return 0;
}
