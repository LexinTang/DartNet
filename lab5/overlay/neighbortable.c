//FILE: overlay/neighbortable.c
//
//Description: this file the API for the neighbor table
//
//Date: May 03, 2010

#include "neighbortable.h"

//This function first creates a neighbor table dynamically. It then parses the topology/topology.dat file and fill the nodeID and nodeIP fields in all the entries, initialize conn field as -1 .
//return the created neighbor table
nbr_entry_t* nt_create()
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
    int nbrNum = 0;
    while (fgets(line, sizeof(line), fp)) {
        // parse 1st hostname in this line
        char * temp = strtok(line, " ");
        if (strcmp(my_hostname, temp) == 0){
            nbr_name_list[nbrNum] = (char *)malloc(HOSTNAME_LENGTH);
            strcpy(nbr_name_list[nbrNum], strtok(NULL, " "));
            nbrNum++;
            continue;
        }
        // parse 2nd hostname in this line
        else if (strcmp(my_hostname, strtok(NULL, " ")) == 0){
            nbr_name_list[nbrNum] = (char *)malloc(HOSTNAME_LENGTH);
            strcpy(nbr_name_list[nbrNum], temp);
            nbrNum++;
        }
    }
    
    nbr_entry_t * nbr_entry_list = (nbr_entry_t *)malloc(sizeof(nbr_entry_t) * nbrNum);
    for (int i = 0; i < nbrNum; i++){
        nbr_entry_list[i].conn = -1;
        nbr_entry_list[i].nodeID = topology_getNodeIDfromname(nbr_name_list[i]);
        struct hostent *host = gethostbyname(nbr_name_list[i]);
        memmove(&nbr_entry_list[i].nodeIP, host->h_addr_list[0], sizeof(struct in_addr));
        
        free(nbr_name_list[i]);
        nbr_name_list[i] = NULL;
    }
    
    return nbr_entry_list;
}

//This function destroys a neighbortable. It closes all the connections and frees all the dynamically allocated memory.
void nt_destroy(nbr_entry_t* nt)
{
    int nbrNum = topology_getNbrNum();
    for (int i = 0; i < nbrNum; i++){
        if (nt[i].conn != -1){
            close(nt[i].conn);
        }
    }
    free(nt);
    return;
}

//This function is used to assign a TCP connection to a neighbor table entry for a neighboring node. If the TCP connection is successfully assigned, return 1, otherwise return -1
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
    int nbrNum = topology_getNbrNum();
    for (int i = 0; i < nbrNum; i++){
        if (nt[i].nodeID == nodeID){
            nt[i].conn = conn;
            return 1;
        }
    }

    return -1;
}
