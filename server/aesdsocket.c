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

#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 2048

int caugth_sig = 0;

static void signal_handler(int signal_number){
	syslog(LOG_INFO, "Caugth signal, exiting");
	caugth_sig = 1;
}

int main(int argc, char** argv){
	int socketfd, clientfd, filefd;
	struct sockaddr_storage client_addr;
	socklen_t client_address_len;
	struct addrinfo *socket_addrinfo;
	struct addrinfo hints;
	char buffer[BUFFER_SIZE] = "\0";
	char *write_str;

	struct sigaction signal_action;

	int rc;
	int enable = 1;
	struct addrinfo *aux;

	openlog(NULL, 0, LOG_USER);

	memset(&signal_action, 0, sizeof(struct sigaction));
	signal_action.sa_handler = signal_handler;

	if((sigaction(SIGTERM, &signal_action, NULL) == -1) || (sigaction(SIGINT, &signal_action, NULL) == -1)){
		syslog(LOG_ERR, "Failed to setup signal_handler. Errno: %d", errno);
		return -1;
	}

	filefd = open("/var/tmp/aesdsocketdata", O_RDWR|O_CREAT|O_TRUNC|O_SYNC);

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

				// do a while true because string doesnt end on NULL but
				// 	on \n
				while((rc = recv(clientfd, buffer, BUFFER_SIZE-2, 0)) > 0){
					buffer[rc] = '\0';
					if(strchr(buffer, '\n') != NULL){
						write_str = strtok(buffer, "\n");
						syslog(LOG_ERR, "Recved: %s", write_str);
						write(filefd, write_str, strlen(write_str));
						write(filefd, "\n", 1);
						break;
					}else{
						syslog(LOG_ERR, "Recved: %s", buffer);
						write(filefd, buffer, rc);
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
		}/*else{
			int status;
			wait(&status);
		}*/
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
					write(filefd, write_str, strlen(write_str));
					write(filefd, "\n", 1);
					break;
				}else{
					syslog(LOG_ERR, "Not newline found!");
					write(filefd, buffer, rc);
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

	closelog();

	close(socketfd);
	close(filefd);

	remove("/var/tmp/aesdsocketdata");
	freeaddrinfo(socket_addrinfo);
	return 0;
}

