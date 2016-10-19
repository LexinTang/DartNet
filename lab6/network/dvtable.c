
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//This function creates a dvtable(distance vector table) dynamically.
//A distance vector table contains the n+1 entries, where n is the number of the neighbors of this node, and the rest one is for this node itself. 
//Each entry in distance vector table is a dv_t structure which contains a source node ID and an array of N dv_entry_t structures where N is the number of all the nodes in the overlay.
//Each dv_entry_t contains a destination node address the the cost from the source node to this destination node.
//The dvtable is initialized in this function.
//The link costs from this node to its neighbors are initialized using direct link cost retrived from topology.dat. 
//Other link costs are initialized to INFINITE_COST.
//The dynamically created dvtable is returned.
dv_t* dvtable_create()
{
    int nbrNum = topology_getNbrNum();
    dv_t *dv_table = (dv_t *)malloc(sizeof(dv_t) * (nbrNum + 1));
    
    dv_table[0].nodeID = topology_getMyNodeID();

    int nodeNum = topology_getNodeNum();
    dv_table[0].dvEntry = (dv_entry_t *)malloc(sizeof(dv_entry_t) * nodeNum);
    
    int *node_array = topology_getNodeArray();
    for (int i = 0; i < nodeNum; i++){
        dv_table[0].dvEntry[i].nodeID = node_array[i];
        dv_table[0].dvEntry[i].cost = topology_getCost(dv_table[0].nodeID, node_array[i]);
    }
    
    int *nbr_array = topology_getNbrArray();
    for (int i = 1; i < nbrNum + 1; i++){
        dv_table[i].nodeID = nbr_array[i - 1];
        dv_table[i].dvEntry = (dv_entry_t *)malloc(sizeof(dv_entry_t) * nodeNum);
        for (int j = 0; j < nodeNum; j++){
            dv_table[i].dvEntry[j].nodeID = node_array[j];
            dv_table[i].dvEntry[j].cost = INFINITE_COST;
        }
    }
    free(node_array);
    free(nbr_array);
    return dv_table;
}

//This function destroys a dvtable. 
//It frees all the dynamically allocated memory for the dvtable.
void dvtable_destroy(dv_t* dvtable)
{
    int nbrNum = topology_getNbrNum();
    for (int i = 0; i < nbrNum + 1; i++){
        free(dvtable[i].dvEntry);
    }
    free(dvtable);
}

//This function sets the link cost between two nodes in dvtable.
//If those two nodes are found in the table and the link cost is set, return 1.
//Otherwise, return -1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
    int nbrNum = topology_getNbrNum();
    int nodeNum = topology_getNodeNum();
    for (int i = 0; i < nbrNum + 1; i++){
        if (dvtable[i].nodeID == fromNodeID){
            for (int j = 0; j < nodeNum; j++){
                if (dvtable[i].dvEntry[j].nodeID == toNodeID){
                    dvtable[i].dvEntry[j].cost = cost;
                    return 1;
                }
            }
        }
    }
    return -1;
}

//This function returns the link cost between two nodes in dvtable
//If those two nodes are found in dvtable, return the link cost. 
//otherwise, return INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
    int nbrNum = topology_getNbrNum();
    int nodeNum = topology_getNodeNum();
    for (int i = 0; i < nbrNum + 1; i++){
        if (dvtable[i].nodeID == fromNodeID){
            for (int j = 0; j < nodeNum; j++){
                if (dvtable[i].dvEntry[j].nodeID == toNodeID){
                    return dvtable[i].dvEntry[j].cost;
                }
            }
        }
    }
    return INFINITE_COST;
}

//This function prints out the contents of a dvtable.
void dvtable_print(dv_t* dvtable)
{
    int nbrNum = topology_getNbrNum();
    int nodeNum = topology_getNodeNum();
    int myNodeID = topology_getMyNodeID();
    printf("This is distance vector table of %d:\n", myNodeID);
    printf("fromNode  toNode  cost\n");
    for (int i = 0; i < nbrNum + 1; i++){
        for (int j = 0; j < nodeNum; j++){
            printf("%d \t %d \t %u\n", dvtable[i].nodeID, dvtable[i].dvEntry[j].nodeID, dvtable[i].dvEntry[j].cost);
        }
    }
}
