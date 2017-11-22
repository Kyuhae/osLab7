#include <stdio.h>
#include <strings.h>
#include <pthread.h>

#include "babble_registration.h"

client_bundle_t *registration_table[MAX_CLIENT];
int nb_registered_clients;
pthread_rwlock_t regTableLock;

void registration_init(void)
{
	pthread_rwlock_init(&regTableLock, NULL);
    nb_registered_clients=0;

    bzero(registration_table, MAX_CLIENT * sizeof(client_bundle_t*));
}

client_bundle_t* registration_lookup(unsigned long key)
{
	client_bundle_t *tmp = NULL;
	pthread_rwlock_rdlock(&regTableLock);
    int i=0;
    
    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == key){
			tmp = registration_table[i];
        }
    }
	pthread_rwlock_unlock(&regTableLock);
    return tmp;
}

int registration_insert(client_bundle_t* cl)
{   
    /* lookup to find if key already exists*/
    client_bundle_t* lp= registration_lookup(cl->key);
    if(lp != NULL){
        fprintf(stderr, "Error -- id % ld already in use\n", cl->key);
        return -1;
    }

    /* insert cl */
    pthread_rwlock_wrlock(&regTableLock); 
    if(nb_registered_clients == MAX_CLIENT){
        return -1;
    }
    registration_table[nb_registered_clients]=cl;
    nb_registered_clients++;
	pthread_rwlock_unlock(&regTableLock);
    return 0;
}


client_bundle_t* registration_remove(unsigned long key)
{
    int i=0;
    
    pthread_rwlock_wrlock(&regTableLock); 
    for(i=0; i<nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            break;
        }
    }

    if(i == nb_registered_clients){
        fprintf(stderr, "Error -- no client found\n");
        pthread_rwlock_unlock(&regTableLock);
        return NULL;
    }
    
    
    client_bundle_t* cl= registration_table[i];

    nb_registered_clients--;
    registration_table[i] = registration_table[nb_registered_clients];
	pthread_rwlock_unlock(&regTableLock);

    return cl;
}
