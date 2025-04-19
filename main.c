#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define MAX_EVENTS 10

struct request_bundle{
	//request format: 
	//GET(/HEAD/POST/PUt/DELETE/CONNECT/OPTIONS/TRACE/PATCH)
	// /index.html 
	//HTTP/1.1\r\n
	//Host: localhost:4221\r\n
	//User-Agent: curl/7.64.1\r\n
	//Accept: */*\r\n
	//Content-Type: application/octet-stream\r\n
	//Content-Length: 5\r\n\r\n
	//contentofthemessage
	char* method; 		//[8]
	char* target; 		//[256]
	char* host;			//[128]
	char* user_agent; 	//[128]
	char* accept; 		//[256]
	char* conttype;		//[256]
	char* contlen; 		//[16]
	char* body; 		//[2048]
};



int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


struct request_bundle request_parser(char* buffer){
	char* method 	= (char*) malloc(8);
	char* target 	= (char*) malloc(256);
	char* host 		= (char*) malloc(128);
	char* ua		= (char*) malloc(128); 
	char* accept	= (char*) malloc(256);
	char* conttype	= (char*) malloc(256);
	char* contlen	= (char*) malloc(16); 
	char* body	 	= (char*) malloc(2048);
	memset(method,'\x00',8);
	memset(target,'\x00',256);
	memset(host,'\x00',128);
	memset(ua,'\x00',128);
	memset(accept,'\x00',256);
	memset(conttype,'\x00',256);
	memset(contlen,'\x00',16);
	memset(body,'\x00',2048);

	char contbuffer[2048];
	memset(contbuffer,'\x00',2048);

	int i = 0;
	char* tmp = buffer;

	//copy method
	while(*tmp!=' ' && i < 8){
		method[i] = *tmp;
		i = i + 1;
		tmp = tmp + 1;
	}
	method[8] = '\x00';
	if(*tmp != ' '){
		//error
	}

	//copy target
	i = 0;
	tmp = tmp + 1;

	while(*tmp!=' '){
		target[i] = *tmp;
		i = i + 1;
		tmp = tmp + 1;
	}
	if(*tmp != ' '){
		//error
	}

	//skip over "HTTP1.1\r\n"
	i = 0;
	tmp = tmp + 1;
	while(*tmp!='\n'){
		tmp = tmp + 1;
	}
	tmp = tmp + 1;

	//find next \r\n
	char* crnl ="\r\n";
	char twospace[3];
	memset(twospace,'\x00',3);
	i = 0;
	
	//loop through rest of request
	char* dtemp = NULL;
	int j = 0;
	while(1){

		memcpy(twospace,tmp,2);
		if(strcmp(twospace,crnl) == 0){
			tmp = tmp + 2;
			memcpy(twospace,tmp,2);
			//printf("twospace: %02x%02x\n", *tmp,*(tmp +1));

			dtemp = contbuffer;//point to head of content buffer
			j = 0;
			//loop through "[item]: "
			while(*dtemp != ' '){
				dtemp = dtemp + 1;
				j = j + 1;
			}
			
			*dtemp = '\x00'; //set space to \x00 for strcmp;
			dtemp = dtemp + 1;
			j = j + 1;
			if(strcmp(contbuffer,"Host:")==0){
				strcpy(host,dtemp);
			}else if(strcmp(contbuffer,"User-Agent:") == 0){
				strcpy(ua,dtemp);
			}else if(strcmp(contbuffer,"Accept:") == 0){
				strcpy(accept,dtemp);
			}else if(strcmp(contbuffer,"Content-Type:")==0){
				strcpy(conttype,dtemp);
			}else if(strcmp(contbuffer,"Content-Length:") ==0){
				strcpy(contlen,dtemp);
			}else{
				//unknown item;
			}

			//reset content buffer and count
			i = 0;
			memset(contbuffer,'\x00',2048);
			dtemp = NULL;

			if(strcmp(twospace,crnl) == 0){
				tmp = tmp + 2;
				//this happens when we reach '\r\n\r\n', which ends the header
				break;
			}

		}

		contbuffer[i] = *tmp;
		i = i + 1;
		tmp = tmp + 1;
	}
	//tmp is now at head of body
	strcpy(body, tmp);
	


	//populate struct
	struct request_bundle parsedRequest;
	parsedRequest.method = method;
	parsedRequest.target = target;
	parsedRequest.host = host;
	parsedRequest.user_agent = ua;
	parsedRequest.accept = accept;
	parsedRequest.conttype  = conttype;
	parsedRequest.contlen = contlen;
	parsedRequest.body = body;

	return parsedRequest;
}

void get_handler(int new_socket, struct request_bundle c_req, char* directory){
	

	char* tmp = c_req.target;

	//write the first section of the target into a buffer
	char targethead[64];
	memset(targethead,'\x00',64);
	
	int j = 1;
	targethead[0] = '/';
	tmp = tmp + 1;


	while(*tmp!='/' && *tmp!= '\x00'){
		targethead[j] = *tmp;
		tmp = tmp + 1;
		j = j + 1;
	}

	//list of targets
	char* target1 = "/";
	char* target2 = "/echo";
	char* target3 = "/user-agent";
	char* target4 = "/files";

	//list of responses
	char* accept_response = "HTTP/1.1 200 OK\r\n\r\n";//200 message
	char* missing_response = "HTTP/1.1 404 Not Found\r\n\r\n";//404 message
	
	//start of response
	char* resp1 = "HTTP/1.1 200 OK\r\n";
	char* resp2 = "Content-Type: ";
	char* resp3 = "\r\nContent-Length: ";
	char* resp4 = "\r\n\r\n";


	if(strcmp(targethead,target1) == 0){
		//send OK(200) response
		send(new_socket, accept_response, strlen(accept_response), 0);

	}else if(strcmp(targethead, target2) == 0){
		//echo
		//find message content
		int msglen = (int)strlen(c_req.target) - 6;
		char msg_cont[msglen + 1];
		memset(msg_cont,'\x00',msglen + 1);
		memcpy(msg_cont,c_req.target + 6,msglen);

		//convert message length to string (itoa)
		char msglen_itoa[100];
		memset(msglen_itoa,'\x00',100);
		sprintf(msglen_itoa,"%d",msglen);

		//piece entire reponse together
		char resp[4096];
		memset(resp,'\x00',4096);
		strcpy(resp,resp1);
		strcat(resp,resp2);
		strcat(resp,"text/plain");
		strcat(resp,resp3);
		strcat(resp,msglen_itoa);
		strcat(resp,resp4);			
		strcat(resp, msg_cont);

		send(new_socket, resp, strlen(resp), 0);

	}else if(strcmp(targethead, target3) == 0){
		//user-agent
		//find message content
		int msglen = (int)strlen(c_req.user_agent);

		//convert message length to string (itoa)
		char msglen_itoa[100];
		memset(msglen_itoa,'\x00',100);
		sprintf(msglen_itoa,"%d",msglen);

		//piece entire reponse together
		char resp[4096];
		memset(resp, '\x00', 4096);
		strcpy(resp,resp1);
		strcat(resp,resp2);
		strcat(resp,"text/plain");
		strcat(resp,resp3);
		strcat(resp,msglen_itoa);
		strcat(resp,resp4);			
		strcat(resp, c_req.user_agent);

		send(new_socket, resp, strlen(resp), 0);

		//printf("target is user-agent");
		
	}else if(strcmp(targethead, target4) == 0){
		//files
		//find file requested
		char filename[512];
		memset(filename,'\x00', 512);	
		strcpy(filename, directory);
		strcat(filename, c_req.target + 7);

		FILE* fileptr = fopen((const char*) filename, "r");

		if(fileptr != NULL){

			fseek(fileptr, 0,SEEK_END);
			int filelength = ftell(fileptr);
			fseek (fileptr, 0, SEEK_SET);
			char filebuffer[filelength];
			fread(filebuffer, 1, filelength, fileptr);
			fclose(fileptr);

			//convert file length to string (itoa)
			char msglen_itoa[100];
			memset(msglen_itoa,'\x00',100);
			sprintf(msglen_itoa,"%d",filelength);


			char resp[4096];
			memset(resp,'\x00',4096);
			strcpy(resp,resp1);
			strcat(resp,resp2);
			strcat(resp,"application/octet-stream");
			strcat(resp,resp3);
			strcat(resp,msglen_itoa);
			strcat(resp,resp4);
			strcat(resp,filebuffer);
			
			
			send(new_socket, resp, strlen(resp), 0);


		}else{
			send(new_socket, missing_response, strlen(missing_response), 0);
		}

	}else{
		//send cannot find(404) response
		send(new_socket, missing_response, strlen(missing_response), 0);

	}
}

void post_handler(int new_socket, struct request_bundle c_req, char* directory){
	
	char* create_response = "HTTP/1.1 201 Created\r\n\r\n";//201 message
	char* target4 = "/files";

	//create directory
	//argv[2] passes in directory as "/[directory]/", i.e., final slash
	//remove final slash:
	int dirname_len = (int) strlen(directory);
	directory[dirname_len - 1] = '\x00';

	//make directory if it does not already exist
	struct stat st = {0};
	if(stat(directory, &st) == -1){
		mkdir(directory, 0700);
	}
		
	//write the first section of the target into a buffer
	char targethead[64];
	memset(targethead,'\x00',64);
	

	char* tmp = c_req.target;
	int j = 1;
	targethead[0] = '/';
	tmp = tmp + 1;

	while(*tmp!='/' && *tmp!= '\x00'){
		targethead[j] = *tmp;
		tmp = tmp + 1;
		j = j + 1;
	}
	
	//if target is in "/files", create and write file
	if(strcmp(targethead,target4) == 0){

		char filepath[256];
		memset(filepath,'\x00',256);
		strcpy(filepath,directory);
		strcat(filepath,tmp);//tmp starts at '/'

		//create file
		FILE* fileptr = fopen((const char *)filepath,"w");
		if(fileptr == NULL){
			perror("Error opening file");
			exit(1);
		}

		//write into file
		int filelength = atoi(c_req.contlen);
		fwrite(c_req.body, 1, filelength ,fileptr);
		
		//close file
		fclose(fileptr);
			
		//send Created(201) response
		send(new_socket, create_response, strlen(create_response), 0);

	}

}


int main(int argc, char* argv[]) {

	
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	printf("Logs from your program will appear here!\n");

	int server_fd, new_socket;
	
	//socket creation
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	//optional: prevents "address already in use" error
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	//bind socket to address and port
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	//waits for client to make connection
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	//ASYNC handling of multiple requests using epoll

    set_nonblock(server_fd);

    int epoll_fd = epoll_create1(0);
    struct epoll_event event = {
        .events = EPOLLIN | EPOLLET,
        .data.fd = server_fd
    };
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    struct epoll_event events[MAX_EVENTS];

	while(1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for(int i = 0; i < nfds; i++) {
            if(events[i].data.fd == server_fd) {

				struct sockaddr_in client_addr;
				int client_addr_len;

				printf("Waiting for a client to connect...\n");
				client_addr_len = sizeof(client_addr);
				
				//accept first connection request in queue of pending connections
				if((new_socket = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len)) < 0){
					printf("Accept failed: %s \n", strerror(errno));
					return 1;
				}
				printf("Client connected\n");
				set_nonblock(new_socket);

				struct epoll_event client_event = {
				.events = EPOLLIN | EPOLLET | EPOLLRDHUP,
				.data.fd = new_socket
				};
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &client_event);

			} else{
                // Handle client data
                char buffer[BUFFER_SIZE];
				memset(buffer, '\x00', BUFFER_SIZE);
                ssize_t count;
                
                while((count = read(events[i].data.fd, buffer, BUFFER_SIZE)) > 0) {

					//read in client request
					struct request_bundle c_req = request_parser(buffer);

					//handle get request
					if(strcmp(c_req.method,"GET")==0){
						get_handler(events[i].data.fd, c_req, argv[2]);
					}
					//handle post request
					else if(strcmp(c_req.method,"POST")==0){
						post_handler(events[i].data.fd, c_req, argv[2]);
					}

					//free all allocated memory and set pointers to NULL
					free(c_req.method);
					free(c_req.target);
					free(c_req.host);
					free(c_req.user_agent);
					free(c_req.conttype);
					free(c_req.contlen);
					free(c_req.body);
					c_req.method = NULL;
					c_req.target = NULL;
					c_req.host = NULL;
					c_req.user_agent = NULL;
					c_req.conttype = NULL;
					c_req.contlen = NULL;
					c_req.body = NULL;



                }
                
                if(count == 0 || (count == -1 && errno != EAGAIN)) {
                    // Close connection on error or EOF
                    close(events[i].data.fd);
                }
            }

		}

	}

	//all done
	close(server_fd);

	return 0;

}


