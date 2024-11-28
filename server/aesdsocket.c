#include <stdio.h>
#include <sys/socket.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 2048

int caugth_sig = 0;
pthread_mutex_t mutex;

struct thread_data_s{
	int clientfd;
	int filefd;
};

struct connection_data_s{
	pthread_t thread_id;
	struct thread_data_s t_data;

	SLIST_ENTRY(connection_data_s) entries;
};
SLIST_HEAD(slisthead, connection_data_s) head;

static void signal_handler(int signal_number){
	syslog(LOG_INFO, "Caugth signal, exiting");
	caugth_sig = 1;
}

void* thread_handle_connection(void* thread_arg){
	char buffer[BUFFER_SIZE] = "\0";
	char *write_str;

	int rc = 0;
	int ret = 0;

	struct thread_data_s* t_data;
	t_data = (struct thread_data_s*)(thread_arg);

	pthread_mutex_lock(&mutex);
	// do a while true because string doesnt end on NULL but
	// 	on \n
	while((rc = recv(t_data->clientfd, buffer, BUFFER_SIZE-2, 0)) > 0){
		buffer[rc] = '\0';
		if(strchr(buffer, '\n') != NULL){
			write_str = strtok(buffer, "\n");
			syslog(LOG_ERR, "Recved: %s", write_str);
			ret = write(t_data->filefd, write_str, strlen(write_str));
			ret = write(t_data->filefd, "\n", 1);
			break;
		}else{
			syslog(LOG_ERR, "Recved: %s", buffer);
			ret = write(t_data->filefd, buffer, rc);
		}
	}

	// set offset to beginning of file
	lseek(t_data->filefd, 0, SEEK_SET);
	while((rc = read(t_data->filefd, buffer, BUFFER_SIZE-2)) > 0){
		buffer[rc] = '\0';
		send(t_data->clientfd, buffer, rc, 0);
	}
	pthread_mutex_unlock(&mutex);

	close(t_data->clientfd);
	//syslog(LOG_INFO, "Closed connection from ", client_addr->sin6_addr);
	
	if(ret == -1)
		return NULL;
	
	return NULL;
}

int main(int argc, char** argv){
	int socketfd, clientfd, filefd;
	char buffer[BUFFER_SIZE] = "\0";
	char *write_str;
	struct sockaddr_storage client_addr;
	socklen_t client_address_len;
	struct addrinfo *socket_addrinfo;
	struct addrinfo hints;

	struct sigaction signal_action;

	int rc;
	int enable = 1;
	struct addrinfo *aux;

	int ret = 0;

	struct slisthead head;
	struct connection_data_s* aux_data;

	openlog(NULL, 0, LOG_USER);

	pthread_mutex_init(&mutex, NULL);

	memset(&signal_action, 0, sizeof(struct sigaction));
	signal_action.sa_handler = signal_handler;

	if((sigaction(SIGTERM, &signal_action, NULL) == -1) || (sigaction(SIGINT, &signal_action, NULL) == -1)){
		syslog(LOG_ERR, "Failed to setup signal_handler. Errno: %d", errno);
		return -1;
	}

	filefd = open("/var/tmp/aesdsocketdata", O_RDWR|O_CREAT|O_TRUNC|O_SYNC, 0777);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	rc = getaddrinfo(NULL, "9000", &hints, &socket_addrinfo);
	if(rc != 0){
		syslog(LOG_ERR, "Failed to get addrinfo. Error code: %s", gai_strerror(rc));
		return -1;
	}

	socketfd = socket(AF_INET6, SOCK_STREAM, 0);
	if(socketfd == -1){
		syslog(LOG_ERR, "Failed to create socket fd. Errno: %d", errno);
		freeaddrinfo(socket_addrinfo);
		remove("/var/tmp/aesdsocketdata");
		return -1;
	}

	// set REUSEADDR to not get EADDRINUSE when valgrind restarts the server
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
	    perror("setsockopt(SO_REUSEADDR) failed");
	}

	int opts;
	opts = fcntl(socketfd, F_GETFL);
	opts = opts & (~O_NONBLOCK);
	fcntl(socketfd, F_SETFL, opts);

	for(aux = socket_addrinfo; aux != NULL; aux = aux->ai_next){
		if(bind(socketfd, socket_addrinfo->ai_addr, socket_addrinfo->ai_addrlen) == -1){
			syslog(LOG_ERR, "Failed to bind socket. Errno: %d. Retrying.", errno);
			continue;
		}

		break;
	}

	if(aux == NULL){
		syslog(LOG_ERR, "Failed to bind socket. Leaving.");
		remove("/var/tmp/aesdsocketdata");
		return -1;
	}

	if(argc > 1 && strcmp(argv[1], "-d") == 0){
		syslog(LOG_ERR, "Running as a daemon!");

		int pid = fork();

		if(pid == 0) {
			if(listen(socketfd, MAX_CONNECTIONS) == -1){
				syslog(LOG_ERR, "Failed to listen for connections. Errno: %d", errno);
				remove("/var/tmp/aesdsocketdata");
				return -1;
			}

			client_address_len = sizeof(client_addr);
			while(!caugth_sig){
				syslog(LOG_ERR, "Accepting connection.");
				clientfd = accept(socketfd, (struct sockaddr*)&client_addr, &client_address_len);
				if(clientfd == -1){
					syslog(LOG_ERR, "Failed to create socket fd. Errno: %d", errno);
					// accept was interrupted by a signal
					if(errno == EINTR || caugth_sig){
						break;
					}
					freeaddrinfo(socket_addrinfo);
					remove("/var/tmp/aesdsocketdata");
					return -1;
				}
				//syslog(LOG_ERR, "Accepted connection from ", client_addr->sin6_addr);

				// recreate the object because we are passing addresses to the thread
				// there'll be no leak because the list will handle the addresses
				aux_data = malloc(sizeof(struct connection_data_s));
				aux_data->t_data.filefd = filefd;
				aux_data->t_data.clientfd = clientfd;

				pthread_create(&aux_data->thread_id, NULL, thread_handle_connection, &aux_data->t_data);

				SLIST_INSERT_HEAD(&head, aux_data, entries);

			}

			struct connection_data_s* np;

			SLIST_FOREACH(np, &head, entries)
				pthread_join(np->thread_id, NULL);

			/*while(!SLIST_EMPTY(&head)){
				aux_data = SLIST_FIRST(&head);
				SLIST_REMOVE_HEAD(&head, entries);
				if(aux_data != NULL)
					free(aux_data);
			}*/
		}else{
			/*int status;

			wait(&status);*/

			clock_t prev_time, curr_time;
			time_t get_t;
			char rfc_2822[BUFFER_SIZE];

			prev_time = clock();
			while(!caugth_sig){
				curr_time = clock();
				if(((((int)(curr_time - prev_time))/CLOCKS_PER_SEC) > 10)){
					prev_time = curr_time;
					pthread_mutex_lock(&mutex);
					get_t = time(NULL);
					strftime(rfc_2822, sizeof(rfc_2822), "%a, %d %b %Y %T %z", localtime(&get_t));
					ret = write(filefd, "timestamp:", strlen(rfc_2822));
					ret = write(filefd, rfc_2822, strlen(rfc_2822));
					ret = write(filefd, "\n", 1);
					pthread_mutex_unlock(&mutex);
				}
			}
		}
	}else{
		if(listen(socketfd, MAX_CONNECTIONS) == -1){
			syslog(LOG_ERR, "Failed to listen for connections. Errno: %d", errno);
			remove("/var/tmp/aesdsocketdata");
			return -1;
		}

		client_address_len = sizeof(client_addr);
		while(!caugth_sig){
			syslog(LOG_ERR, "Accepting connection.");
			clientfd = accept(socketfd, (struct sockaddr*)&client_addr, &client_address_len);
			if(clientfd == -1){
				syslog(LOG_ERR, "Failed to create socket fd. Errno: %d", errno);
				// accept was interrupted by a signal
				if(errno == EINTR || caugth_sig){
					break;
				}
				freeaddrinfo(socket_addrinfo);
				remove("/var/tmp/aesdsocketdata");
				return -1;
			}
			//syslog(LOG_ERR, "Accepted connection from ", client_addr->sin6_addr);

			// do a while true because string doesnt end on NULL but
			// 	on \n
			while((rc = recv(clientfd, buffer, BUFFER_SIZE-2, 0)) > 0){
				buffer[rc] = '\0';
				syslog(LOG_ERR, "Recved: %s", buffer);
				if(strchr(buffer, '\n') != NULL){
					write_str = strtok(buffer, "\n");
					ret = write(filefd, write_str, strlen(write_str));
					ret = write(filefd, "\n", 1);
					break;
				}else{
					syslog(LOG_ERR, "Not newline found!");
					ret = write(filefd, buffer, rc);
				}
			}

			// set offset to beginning of file
			lseek(filefd, 0, SEEK_SET);
			while((rc = read(filefd, buffer, BUFFER_SIZE-2)) > 0){
				buffer[rc] = '\0';
				send(clientfd, buffer, rc, 0);
			}

			close(clientfd);
			//syslog(LOG_INFO, "Closed connection from ", client_addr->sin6_addr);
		}
	}

	pthread_mutex_destroy(&mutex);

	closelog();

	close(socketfd);
	close(filefd);

	remove("/var/tmp/aesdsocketdata");
	freeaddrinfo(socket_addrinfo);

	if(ret == -1)
		return -1;
	return 0;
}

