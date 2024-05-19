#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

// TODO: remake this with nftw

#define MAX_READ 256

void recursive_searchdir(const char* searchdir, const char* searchstr, int* acc){
	int fd;
	char buffer[MAX_READ];
	char *string, path[MAX_READ];
	DIR* dir;
	struct dirent* curr_dir;
	
	// argv[0] == ./finder; argv[1] == <filesdir>; argv[2]; == searchstr;
	if(!(dir = opendir(searchdir))){
		syslog(LOG_ERR, "Failed to opendir %s: Error: %s", searchdir, strerror(errno));
	}

	// ignore myself and parent
	while(curr_dir = readdir(dir)){
		syslog(LOG_ERR, "Read file: %s", curr_dir->d_name);
		snprintf(path, MAX_READ, "%s/%s", searchdir, curr_dir->d_name);
		if(strcmp(curr_dir->d_name, ".") && strcmp(curr_dir->d_name, "..")){
			syslog(LOG_ERR, "Process file: %s", curr_dir->d_name);
			if(curr_dir->d_type == DT_DIR){
				// if this is a dir, recursively search it (depth-first)
				syslog(LOG_ERR, "Recursively calling for %s", curr_dir->d_name);
				recursive_searchdir(path, searchstr, acc);
			}else if(curr_dir->d_type == DT_REG){
				// if its a regular file, read it
				fd = open(path, O_RDONLY);
				while(read(fd, buffer, MAX_READ)){
					// tokenize read buffer to check for searchstr
					string = strtok(buffer, " \n");
					do{
						if(!strcmp(string, searchstr))
							acc[0]++;
					}while(string = strtok(NULL, " \n"));
				}
				close(fd);
				acc[1]++;
			}else{
				// i don't think reading block devices, sockets, etc. should be supported
				syslog(LOG_ERR, "This is not a regular file nor a directory!");
				closedir(dir);
				return;
			}
		}
	}

	closedir(dir);

	return;
}

int main(int argc, char* argv[]){
	int* output = malloc(sizeof(int)*2);

	openlog(NULL, 0, LOG_USER);

	if(argc != 3){
		syslog(LOG_ERR, "Invalid number of arguments in argc: %d", argc);
		syslog(LOG_ERR, "Usage: ./finder <filesdir> <searchstr>");
		return 1;
	}
	
	recursive_searchdir(argv[1], argv[2], output);

	syslog(LOG_DEBUG, "The number of files are %d and the number of matching lines are %d", output[1], output[0]);

	return 0;
}
