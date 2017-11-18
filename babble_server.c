#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <strings.h>

#include <pthread.h>
#include <semaphore.h>

#include "babble_server.h"
#include "babble_types.h"
#include "babble_utils.h"
#include "babble_communication.h"

#define MAX_NB_THREADS 100
#define MAX_NB_CMD 10

//SYNCH
sem_t comThreadSetup, comThreadLimit;
    
    
static void display_help(char *exec)
{
    printf("Usage: %s -p port_number\n", exec);
}


static int parse_command(char* str, command_t *cmd)
{
    /* start by cleaning the input */
    str_clean(str);
    
    /* get command id */
    cmd->cid=str_to_command(str, &cmd->answer_expected);

    /* initialize other fields */
    cmd->answer.size=-1;
    cmd->answer.aset=NULL;

    switch(cmd->cid){
    case LOGIN:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            fprintf(stderr,"Error -- invalid LOGIN -> %s\n", str);
            return -1;
        }
        break;
    case PUBLISH:
        if(str_to_payload(str, cmd->msg, BABBLE_SIZE)){
            fprintf(stderr,"Warning -- invalid PUBLISH -> %s\n", str);
            return -1;
        }
        break;
    case FOLLOW:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            fprintf(stderr,"Warning -- invalid FOLLOW -> %s\n", str);
            return -1;
        }
        break;
    case TIMELINE:
        cmd->msg[0]='\0';
        break;
    case FOLLOW_COUNT:
        cmd->msg[0]='\0';
        break;
    case RDV:
        cmd->msg[0]='\0';
        break;    
    default:
        fprintf(stderr,"Error -- invalid client command -> %s\n", str);
        return -1;
    }

    return 0;
}


static int process_command(command_t *cmd)
{
    int res=0;

    switch(cmd->cid){
    case LOGIN:
        res = run_login_command(cmd);
        break;
    case PUBLISH:
        res = run_publish_command(cmd);
        break;
    case FOLLOW:
        res = run_follow_command(cmd);
        break;
    case TIMELINE:
        res = run_timeline_command(cmd);
        break;
    case FOLLOW_COUNT:
        res = run_fcount_command(cmd);
        break;
    case RDV:
        res = run_rdv_command(cmd);
        break;
    default:
        fprintf(stderr,"Error -- Unknown command id\n");
        return -1;
    }

    if(res){
        fprintf(stderr,"Error -- Failed to run command ");
        display_command(cmd, stderr);
    }

    return res;
}

/* sends an answer for the command to the client if needed */
/* answer to a command is stored in cmd->answer after the command has
 * been processed. They are different cases
 + The client does not expect any answer (then nothing is sent)
 + The client expect an answer -- 2 cases
  -- The answer is a single msg
  -- The answer is potentially composed of multiple msgs (case of a timeline)
*/
static int answer_command(command_t *cmd)
{    
    /* case of no answer requested by the client */
    if(!cmd->answer_expected){
        if(cmd->answer.aset != NULL){
            free(cmd->answer.aset);
        }
        return 0;
    }
    
    /* no msg to be sent */
    if(cmd->answer.size == -2){
        return 0;
    }

    /* a single msg to be sent */
    if(cmd->answer.size == -1){
        /* strlen()+1 because we want to send '\0' in the message */
        if(write_to_client(cmd->key, strlen(cmd->answer.aset->msg)+1, cmd->answer.aset->msg)){
            fprintf(stderr,"Error -- could not send ack for %d\n", cmd->cid);
            free(cmd->answer.aset);
            return -1;
        }
        free(cmd->answer.aset);
        return 0;
    }
    

    /* a set of msgs to be sent */
    /* number of msgs sent first */
    if(write_to_client(cmd->key, sizeof(int), &cmd->answer.size)){
        fprintf(stderr,"Error -- send set size: %d\n", cmd->cid);
        return -1;
    }

    answer_t *item = cmd->answer.aset, *prev;
    int count=0;

    /* send only the last BABBLE_TIMELINE_MAX */
    int to_skip= (cmd->answer.size > BABBLE_TIMELINE_MAX)? cmd->answer.size - BABBLE_TIMELINE_MAX : 0;

    for(count=0; count < to_skip; count++){
        prev=item;
        item = item->next;
        free(prev);
    }
    
    while(item != NULL ){
        if(write_to_client(cmd->key, strlen(item->msg)+1, item->msg)){
            fprintf(stderr,"Error -- could not send set: %d\n", cmd->cid);
            return -1;
        }
        prev=item;
        item = item->next;
        free(prev);
        count++;
    }

    assert(count == cmd->answer.size);
    return 0;
}
//-----------------------   PRODUCER / CONSUMER  --------------------------//
typedef struct syncBufferS {
	sem_t produceable, consumeable;
	pthread_mutex_t mutex;;
	int count;
	command_t *buf[MAX_NB_CMD];
} syncBuffer;

syncBuffer *command_buffer;


syncBuffer * syncBuffer_init(void) {
	syncBuffer *sBuf = malloc(sizeof(struct syncBufferS));
	if (sBuf == NULL) {
		printf("sBuf init MALLOC SCREWED UP!\n");
	}
	sem_init(&(sBuf->produceable), 0, MAX_NB_CMD);
	sem_init(&(sBuf->consumeable), 0, 0);
	pthread_mutex_init(&(sBuf->mutex), NULL);
	sBuf->count = 0;
	return sBuf;
}

void syncBuffer_put(syncBuffer *sBuf, command_t *cmd) {
    sem_wait(&(sBuf->produceable));
    pthread_mutex_lock(&(sBuf->mutex));
    
    sBuf->count++; 
    //TODO: implement copy from cmd to the new command
    sBuf->buf[sBuf->count - 1] = cmd;
    //memcpy(sBuf->buf[sBuf->count - 1], cmd, sizeof(struct command));
    //memcpy(sBuf->buf[sBuf->count - 1]->anwser, cmd->answer, sizeof(struct answer));
    /*sBuf->buf[sBuf->count - 1] = new_command(cmd->key);
    sBuf->buf[sBuf->count - 1]->cid = cmd->cid;
    sBuf->buf[sBuf->count - 1]->sock = 0;
    sBuf->buf[sBuf->count - 1]->key = cmd->key;
    strncpy(sBuf->buf[sBuf->count - 1]->msg, cmd->msg, BABBLE_ID_SIZE);*/
    

    
    
    /*
     *     command_id cid;
    int sock;    /only needed by the LOGIN command, other commands
                  *will use the key
    unsigned long key;
    char msg[BABBLE_SIZE];
    answer_set_t answer; /once the cmd has been processed, answer
                            to client is stored there 
    int answer_expected;    answer sent only if set 
    //copy stuff here
    * 
    * */
    printf("pushing command for client key %lu\n",cmd->key);
    
    pthread_mutex_unlock(&(sBuf->mutex));
    sem_post(&(sBuf->consumeable));
}

command_t * syncBuffer_get(syncBuffer *sBuf) {
    command_t *cmd;
    sem_wait(&(sBuf->consumeable));
    pthread_mutex_lock(&(sBuf->mutex));
    
    cmd = sBuf->buf[sBuf->count - 1];
    sBuf->count--;
    printf("\t fetched command for client key %lu\n",cmd->key);
    
    pthread_mutex_unlock(&(sBuf->mutex));
    sem_post(&(sBuf->produceable));
    return cmd;
}

//-----------------------  END PRODUCER / CONSUMER END --------------------------//

void* communicationT(void *data) {
	unsigned long client_key=0;
	char* recv_buff=NULL;
	int recv_size=0;
	
	//copy input parameter
	int newsockfd = (*(int*)data);
	char client_name[BABBLE_ID_SIZE+1];
		//we're done copying, signal main so it can create another commThread
	sem_post(&comThreadSetup);
	
	//process login command
	bzero(client_name, BABBLE_ID_SIZE+1);
	if((recv_size = network_recv(newsockfd, (void**)&recv_buff)) < 0){
		fprintf(stderr, "Error -- recv from client\n");
		close(newsockfd);
	}

	command_t *cmd = new_command(0);
	
	if(parse_command(recv_buff, cmd) == -1 || cmd->cid != LOGIN){
		fprintf(stderr, "Error -- in LOGIN message\n");
		close(newsockfd);
		free(cmd);
	}

	/* before processing the command, we should register the
	 * socket associated with the new client; this is to be done only
	 * for the LOGIN command */
	cmd->sock = newsockfd;

	if(process_command(cmd) == -1){
		fprintf(stderr, "Error -- in LOGIN\n");
		close(newsockfd);
		free(cmd);    
	}

	/* notify client of registration */
	if(answer_command(cmd) == -1){
		fprintf(stderr, "Error -- in LOGIN ack\n");
		close(newsockfd);
		free(cmd);
	}

	/* let's store the key locally */
	client_key = cmd->key;

	strncpy(client_name, cmd->msg, BABBLE_ID_SIZE);
	free(recv_buff);
	free(cmd);
	

	/* looping on client commands */
	while((recv_size=network_recv(newsockfd, (void**) &recv_buff)) > 0){
		cmd = new_command(client_key);
		if(parse_command(recv_buff, cmd) == -1){
			fprintf(stderr, "Warning: unable to parse message from client %s\n", client_name);
			notify_parse_error(cmd, recv_buff);
		}
		//put command into command_buffer
		syncBuffer_put(command_buffer, cmd);
		/* NOPE, now the executor will do this
		 * else{
			if(process_command(cmd) == -1){
				fprintf(stderr, "Warning: unable to process command from client %lu\n", client_key);
			}
			if(answer_command(cmd) == -1){
				fprintf(stderr, "Warning: unable to answer command from client %lu\n", client_key);
			}
		}*/
	}
	printf("client_name = %s\n",client_name);
	if(client_name[0] != 0){
		cmd = new_command(client_key);
		cmd->cid= UNREGISTER;
		
		if(unregisted_client(cmd)){
			fprintf(stderr,"Warning -- failed to unregister client %s\n",client_name);
		}
		free(cmd);
		sem_post(&comThreadLimit);
	}
	return NULL;
}

void* executorT(void *data) {
	syncBuffer *command_buffer = (syncBuffer *)(data);
	command_t *cmd;
	while(1) {
		//wait for there to be something in buffer
		//grab cmd from buffer
		cmd = syncBuffer_get(command_buffer);
		// !need to implement R/W lock on registration table
		
		if(process_command(cmd) == -1){
			fprintf(stderr, "Warning: unable to process command from client %lu\n", cmd->key);
		}
		if(answer_command(cmd) == -1){
			fprintf(stderr, "Warning: unable to answer command from client %lu\n", cmd->key);
		}
	}
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd;
    int portno=BABBLE_PORT;
    
    int opt, i;
    int nb_args=1;
    command_buffer = syncBuffer_init();
    if (command_buffer == NULL) {
		printf("COMMAND_BUFFER NULLPTR\n");
	}
	
    //VARS MOVED TO GLOBAL AT TOP OF FILE
    
    pthread_t *comTids = malloc(BABBLE_COMMUNICATION_THREADS * sizeof(pthread_t));
    pthread_t *exeTids = malloc(BABBLE_EXECUTOR_THREADS * sizeof(pthread_t));
    int comInd = 0;
	int exeInd = 0;
    
    //SYNCH
    sem_init(&comThreadSetup, 0, 1);
    sem_init(&comThreadLimit, 0, BABBLE_COMMUNICATION_THREADS);
    

    while ((opt = getopt (argc, argv, "+p:")) != -1){
        switch (opt){
        case 'p':
            portno = atoi(optarg);
            nb_args+=2;
            break;
        case 'h':
        case '?':
        default:
            display_help(argv[0]);
            return -1;
        }
    }
    
    if(nb_args != argc){
        display_help(argv[0]);
        return -1;
    }

    server_data_init();

    if((sockfd = server_connection_init(portno)) == -1){
        return -1;
    }

    printf("Babble server bound to port %d\n", portno);   
    
//CREATE EXECUTOR THREAD HERE 
	for (i=0; i < BABBLE_EXECUTOR_THREADS; i++) {
		pthread_create (&exeTids[(exeInd % BABBLE_EXECUTOR_THREADS)-1], NULL, executorT, command_buffer) ;
	}	
		
		
    /* main server loop */
    while(1){
		//Can't touch newsockfd until all comm threads have finished making copies
		sem_wait(&comThreadSetup);
        if((newsockfd= server_connection_accept(sockfd))==-1){
            return -1;
        }
		//CREATE COMMS THREAD HERE  (if there are already max num then wait for one to end);
		sem_wait(&comThreadLimit);
		pthread_create (&comTids[(comInd % BABBLE_COMMUNICATION_THREADS)-1], NULL, communicationT, &newsockfd) ;
		comInd++;
		
    }
    close(sockfd);
    return 0;
}
