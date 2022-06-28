#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h> 
#include <fcntl.h>
#include <sys/stat.h> 

#define PIPE_NAME "input_pipe" 

int main() {
	int fd = open(PIPE_NAME, O_WRONLY|O_NONBLOCK);
	char n[50];
	while(1){
		printf("introduza um comando:\n");
		gets(n);
		write(fd, n, 50*sizeof(char));
		printf("comando escrito no pipe\n");
	} 
	close(fd);
	return 0;
}
