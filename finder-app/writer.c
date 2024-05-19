#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

int main(int argc, char* argv[]){
	FILE* writefile;

	openlog(NULL, 0, LOG_USER);

	if(argc != 3){
		syslog(LOG_ERR, "Invalid number of arguments in argc: %d", argc);
		syslog(LOG_ERR, "Usage: ./writer <writefile> <writestr>");
		return 1;
	}

	if(!(writefile = fopen(argv[1], "w+"))){
		syslog(LOG_ERR, "Could not create file %s. Error: %s", argv[1], strerror(errno));
	}

	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
	fwrite(argv[2], strlen(argv[2]), 1, writefile);
	
	return 0;
}
