
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//This function creates a neighbor cost table dynamically 
//and initialize the table with all its neighbors' node IDs and direct link costs.
//The neighbors' node IDs and direct link costs are retrieved from topology.dat file. 
nbr_cost_entry_t* nbrcosttable_create()
{
    int nbrNum = topology_getNbrNum();
    nbr_cost_entry_t *cost_table = (nbr_cost_entry_t *)malloc(sizeof(nbr_cost_entry_t) * nbrNum);
    //printf("nbrNum = %d\n", nbrNum);
    
    // initialize the table with neighbors' node IDs and direct link costs
    int myNodeID = topology_getMyNodeID();
    int *nbr_array = topology_getNbrArray();
    //printf("jump into for loop\n");
    for (int i = 0; i < nbrNum; i++){
        cost_table[i].nodeID = nbr_array[i];
        cost_table[i].cost = topology_getCost(myNodeID, nbr_array[i]);
    }
    //printf("freeing nbr_array\n");
    free(nbr_array);
    
    return cost_table;
}

//This function destroys a neighbor cost table. 
//It frees all the dynamically allocated memory for the neighbor cost table.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
    free(nct);
}

//This function is used to get the direct link cost from neighbor.
//The direct link cost is returned if the neighbor is found in the table.
//INFINITE_COST is returned if the node is not found in the table.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
    int nbrNum = topology_getNbrNum();
    for (int i = 0; i < nbrNum; i++){
        if (nct[i].nodeID == nodeID){
            return nct[i].cost;
        }
    }
    return INFINITE_COST;
}

//This function prints out the contents of a neighbor cost table.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
    int nbrNum = topology_getNbrNum();
    int myNodeID = topology_getMyNodeID();
    printf("This is neighbor cost table of %d:\n", myNodeID);
    printf("Node   cost\n");
    for (int i = 0; i < nbrNum; i++){
        printf("%u \t %u \n", nct[i].nodeID, nct[i].cost);
    }
}
