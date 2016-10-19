//FILE: topology/topology.c
//
//Description: this file implements some helper functions used to parse 
//the topology file 
//
//Date: May 3,2010

#include "../common/constants.h"
#include "topology.h"

//this function returns node ID of the given hostname
//the node ID is an integer of the last 8 digit of the node's IP address
//for example, a node with IP address 202.120.92.3 will have node ID 3
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromname(char* hostname) 
{
    struct hostent *host;
    if ((host = gethostbyname(hostname)) == NULL){
        printf("Cannot resolve hostname, %s\n", hostname);
        return - 1;
    }
    
    struct in_addr addr;
    memmove(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    char ip[16];
    strcpy(ip, inet_ntoa(addr));
    //printf("ip = %s\n", ip);
    strtok(ip, ".");
    strtok(NULL, ".");
    strtok(NULL, ".");
    char *node_id;
    node_id = strtok(NULL, "\0");
    //printf("node_id = %s\n", node_id);
    int nodeID = atoi(node_id);
    return nodeID;
}

//this function returns node ID from the given IP address
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromip(struct in_addr* addr)
{
    char ip[16];
    if (inet_ntoa(*addr) == NULL){
        printf("Cannot resolve addr\n");
        return - 1;
    }
    strcpy(ip, inet_ntoa(*addr));
    //printf("ip = %s\n", ip);
    strtok(ip, ".");
    strtok(NULL, ".");
    strtok(NULL, ".");
    char *node_id;
    node_id = strtok(NULL, "\0");
    //printf("node_id = %s\n", node_id);
    int nodeID = atoi(node_id);
    return nodeID;
}

//this function returns my node ID
//if my node ID can't be retrieved, return -1
int topology_getMyNodeID()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0){
        printf("Cannot get my hostname!\n");
        return -1;
    }
    
    return topology_getNodeIDfromname(hostname);
}

//this functions parses the topology information stored in topology.dat
//returns the number of neighbors
int topology_getNbrNum()
{
    FILE *fp;
    if ((fp = fopen("../topology/topology.dat", "r")) == NULL) {
        printf("Cannot find topology.dat!\n");
        return -1;
    }
    
    char hostname[HOSTNAME_LENGTH];
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        printf("Cannot get my hostname!\n");
        return -1;
    }
    
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        //printf("%s\n", line);
        if (strcmp(hostname, strtok(line, " ")) == 0){
            count++;
            continue;
        }
        else if (strcmp(hostname, strtok(NULL, " ")) == 0){
            count++;
        }
    }
    
    fclose(fp);
    //printf("count = %d\n", count);
    return count;
}

// This function is used to find out whether hostname is counted before.
// Return 1: hostname is successfullly inserted into the name list
//       -1: hostname already exists
int insert_hostname(char ** hostname_list, char * hostname){
    int i;
    for (i = 0; i < MAX_NODE_NUM; i++){
        if (hostname_list[i] == NULL){
            break;
        }
        if (strcmp(hostname_list[i], hostname) == 0){   // hostname_list[i] and hostname are matched
            break;
        }
    }
    if (i < MAX_NODE_NUM && hostname_list[i] == NULL){
        hostname_list[i] = (char *)malloc(HOSTNAME_LENGTH);
        strcpy(hostname_list[i], hostname);
        //printf("Now adding %s to hostname_list\n", hostname);
        return 1;
    }
    return -1;
}

//this functions parses the topology information stored in topology.dat
//returns the number of total nodes in the overlay 
int topology_getNodeNum()
{
    FILE *fp;
    if ((fp = fopen("../topology/topology.dat", "r")) == NULL) {
        printf("Cannot find topology.dat!\n");
        return -1;
    }

    char *hostname_list[MAX_NODE_NUM];
    char line[256];
    int count = 0;

    memset(hostname_list, 0, sizeof(hostname_list));
    while (fgets(line, sizeof(line), fp)) {
        char *temp;
        temp = strtok(line, " ");
        if (insert_hostname(hostname_list, temp) > 0){
            count++;
        }

        temp = strtok(NULL, " ");
        if (insert_hostname(hostname_list, temp) > 0){
            count++;
        }
    }

    for (int i = 0; i < count; i++){
        free(hostname_list[i]);
        hostname_list[i] = NULL;
    }
    
    //printf("NodeNum = %d\n", count);
    fclose(fp);
    
    return count;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the nodes' IDs in the overlay network  
int* topology_getNodeArray()
{
    FILE *fp;
    if ((fp = fopen("../topology/topology.dat", "r")) == NULL) {
        printf("Cannot find topology.dat!\n");
        return NULL;
    }
    
    char *hostname_list[MAX_NODE_NUM];
    char line[256];
    int count = 0;
    
    // get hostname_list from topology.dat
    memset(hostname_list, 0, sizeof(hostname_list));
    while (fgets(line, sizeof(line), fp)) {
        char *temp;
        temp = strtok(line, " ");
        if (insert_hostname(hostname_list, temp) > 0){
            count++;
        }

        temp = strtok(NULL, " ");
        if (insert_hostname(hostname_list, temp) > 0){
            count++;
        }
    }
    
    if (count == 0){
        printf("0 hostname in topolody.dat!\n");
        return NULL;
    }
    
    // create a dynamically allocated array
    int * nodeID_list = (int *)malloc(sizeof(int) * count);
    
    // map hostname to nodeID and save it into nodeID_list
    for (int i = 0; i < count; i++) {
        int nodeID = topology_getNodeIDfromname(hostname_list[i]);
        nodeID_list[i] = nodeID;
        free(hostname_list[i]);
        hostname_list[i] = NULL;
    }

    //printf("NodeNum = %d\n", count);
    fclose(fp);
    return nodeID_list;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs  
int* topology_getNbrArray()
{
    FILE *fp;
    if ((fp = fopen("../topology/topology.dat", "r")) == NULL) {
        printf("Cannot find topology.dat!\n");
        return NULL;
    }
    
    char my_hostname[HOSTNAME_LENGTH];
    if (gethostname(my_hostname, sizeof(my_hostname)) < 0) {
        printf("Cannot get my hostname!\n");
        return NULL;
    }
    
    char *nbr_name_list[MAX_NODE_NUM];
    memset(nbr_name_list, 0, sizeof(nbr_name_list));

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        // parse 1st hostname in this line
        char * temp = strtok(line, " ");
        if (strcmp(my_hostname, temp) == 0){
            nbr_name_list[count] = (char *)malloc(HOSTNAME_LENGTH);
            strcpy(nbr_name_list[count], strtok(NULL, " "));
            count++;
            continue;
        }
        // parse 2nd hostname in this line
        else if (strcmp(my_hostname, strtok(NULL, " ")) == 0){
            nbr_name_list[count] = (char *)malloc(HOSTNAME_LENGTH);
            strcpy(nbr_name_list[count], temp);
            count++;
        }
    }
    
    // create a dynamically allocated array
    int * nodeID_list = (int *)malloc(sizeof(int) * count);
    
    // map hostname to nodeID and save it into nodeID_list
    for (int i = 0; i < count; i++) {
        int nodeID = topology_getNodeIDfromname(nbr_name_list[i]);
        nodeID_list[i] = nodeID;
        free(nbr_name_list[i]);
        nbr_name_list[i] = NULL;
    }

    fclose(fp);
    //printf("count = %d\n", count);
    return nodeID_list;
}

//this functions parses the topology information stored in topology.dat
//returns the cost of the direct link between the two given nodes 
//if no direct link between the two given nodes, INFINITE_COST is returned
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
    FILE *fp;
    if ((fp = fopen("../topology/topology.dat", "r")) == NULL) {
        printf("Cannot find topology.dat!\n");
        return INFINITE_COST;
    }
    
    // special case!!
    if (fromNodeID == toNodeID){
        return 0;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char * hostname_1 = strtok(line, " ");
        char * hostname_2 = strtok(NULL, " ");
        int nodeID_1 = topology_getNodeIDfromname(hostname_1);
        int nodeID_2 = topology_getNodeIDfromname(hostname_2);

        if ((nodeID_1 == fromNodeID && nodeID_2 == toNodeID)||(nodeID_1 == toNodeID && nodeID_2 == fromNodeID)) {
            char * str_cost = strtok(NULL, "\0");
            fclose(fp);
            return atoi(str_cost);
        }
    }
    fclose(fp);
    return INFINITE_COST;
}
