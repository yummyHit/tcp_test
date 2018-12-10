/*
   This is TCP/IP realization sample. Code write by UkjinJang.
   Using Operating System is Linux; raspbian to raspberry pi.
   I couldn't transfer Layer 4 to Layer 7 at 2016.05.31 (Report by Univ.)
   I'll use this code for spoofing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/types.h>
#include <errno.h>

#define BUF_SIZE 1024
#define FILE_SIZE 10240

char name[30];	// client name variable
char snd_buf[BUF_SIZE], rcv_buf[BUF_SIZE], file_buffer[FILE_SIZE], time_tmp[BUF_SIZE];	// send, receive, file, time-out message buffer
int rcvCnt = 0, checkCnt = 0;	// check variables
int ack[BUF_SIZE] = {0}, seq[BUF_SIZE] = {0}, rcvAck[BUF_SIZE] = {0};	// ack(sequence number + data_size), sequence number, receive ack array
pthread_mutex_t mutx;	// thread variable
struct timeval timeout;
pthread_t thread;	// login client

void * timeo_rcv(void * arg);	// time-out method - running thread
void  flow_start(void * arg);	// flow control start method

void * rcvMessage(void * arg) {	// receive message to server in thread
	int sock = (int)arg;		// socket variable(it's server's socket)
	int ex = 0, i = 0, j = 0, tmp = 0;	// using variables
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	FILE *fp;				// file transfer pointer variable
	while(1) {
		if((ex = recv(sock, rcv_buf, sizeof(rcv_buf), 0)) > 0) {	// recv message to server
			rcv_buf[ex] = '\0';	// end of message, close array
			fflush(stdout);
			if(!strncmp(rcv_buf,"success",7)) {	// if server's message is 'success'
				recv(sock, file_buffer, sizeof(file_buffer), 0);
				fp = fopen("received_file.txt", "a");	// append to message's content at received_file.txt
				fputs(file_buffer, fp);
				fclose(fp);
				printf("\n## File get complete!!\n");
				break;
			}
			else if(!strncmp(rcv_buf, "failed", 6)) {	// else if server's message is 'failed'
				printf("\n## File isn't found.\n## Wait for timeout...\n");
				sprintf(snd_buf, "## Packet loss.. Wait for timeout..\n");
				send(sock, snd_buf, strlen(snd_buf), 0);	// file transfer break!
			}
			else if(!strncmp(rcv_buf, "oversize", 8)) {
				pthread_mutex_lock(&mutx);
				checkCnt--;
				pthread_mutex_unlock(&mutx);
			}
			else if(!strncmp(rcv_buf, "quit", 4)) {		// else if server's message is 'quit'
				printf("\n%s", rcv_buf);		// server log out
				break;
			}
			else if(!strncmp(rcv_buf, "request", 7)) {	// else if server's message is 'request'
				pthread_mutex_lock(&mutx);
				rcvAck[rcvCnt++] = 0;				// run thread that flow control
				pthread_create(&thread, NULL, (void *)timeo_rcv, (void *) sock);
				pthread_mutex_unlock(&mutx);
			}
			else {
				for(i = 0; i < strlen(rcv_buf); i++) {			// server send (abasdsadascad Acknowledge = XXX)
					if(!isdigit(rcv_buf[i]) && rcv_buf[i] == '=') {	// correct = signal
						for(j = i+1; j < strlen(rcv_buf); j++)		// give next to = signal size to j value
							if(isdigit(rcv_buf[j])) {		// tmp have Acknowledge value
								tmp *= 10;	
								tmp += (rcv_buf[j] -'0');
							}
						i = j+1;
					}
				}
				pthread_mutex_lock(&mutx);
				rcvAck[rcvCnt++] = tmp;	// save receive acknowledge size
				checkCnt=0;
				pthread_mutex_unlock(&mutx);
				printf("\n%s", rcv_buf);
				i = 0;	j = 0; tmp = 0;
			}
		}
	}
}

void * sendMessage(void * arg) {		// send to server in thread
	int sock = (int)arg;		// server's socket
	char tmp[BUF_SIZE]; char str_tmp[BUF_SIZE];	// normally chat array - tmp , comparison array - str_tmp
	int i = 0;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	printf("## Name : ");
	pthread_mutex_lock(&mutx);
	fgets(name, 30, stdin);		// name variable is public variable, so must use lock&unlock
	name[strlen(name) -1] = '\0';	// if want use variable between thread - stack 
	pthread_mutex_unlock(&mutx);
	sprintf(snd_buf, "## %s log in\n", name);
	send(sock, snd_buf, strlen(snd_buf), 0);

	while(1) {
		printf("\n## %s >> ", name);
		fgets(tmp, BUF_SIZE, stdin);
		tmp[strlen(tmp)-1] = '\0';
		fflush(stdin);
		if(!strncmp(tmp, "file", 4)) {	// if client enter to 'file'
			sprintf(snd_buf, "%s", tmp);
			send(sock, snd_buf, strlen(snd_buf), 0);
			printf("\n## What's file name : ");	// want file name ask
			scanf("%s",tmp);
			fflush(stdin);
			sprintf(snd_buf, "%s", tmp);
			send(sock, snd_buf, strlen(snd_buf), 0);	// file transfer!!
			continue;
		}
		else if(!strncmp(tmp, "flowStart", 9)) {	// else if client enter to 'flowStart'
			sleep(2);
			sprintf(snd_buf, "%s", tmp);
			send(sock, snd_buf, strlen(snd_buf), 0);
			printf("\n## Flow control start...\n");
			flow_start((void *)sock);	// flow control start
		}
		else if(!strncmp(tmp, "cgstStart", 9)) {	// else if client enter to 'cgstStart'
			sprintf(snd_buf, "%s", tmp);
			send(sock, snd_buf, strlen(snd_buf), 0);
			printf("\n## Congestion control start...\n");
			sleep(1);
			sprintf(snd_buf, "%d", BUF_SIZE);
			send(sock, snd_buf, strlen(snd_buf), 0);	// server will start congestion control
		}
		else if(!strncmp(tmp, "quit", 4)) {		 // else if client enter to 'quit'
			sprintf(snd_buf, "## Quit\n## %s log out\n", name);
			send(sock, snd_buf, strlen(snd_buf), 0);	// break the send thread
			break;
		}
		else {
			if(checkCnt < 0) {	// Over the rwnd size, don't send message.
				if(checkCnt == -1) {
					i--;		// sequence number change declare
					pthread_mutex_lock(&mutx);
					checkCnt--;
					pthread_mutex_unlock(&mutx);
				}
				printf("\n## %s >> Rwnd size overflow. If you receive ack, then try again.\n", name);
				continue;
			}
			sprintf(str_tmp, "## %s >> %s\n", name, tmp);
			pthread_mutex_lock(&mutx);
			ack[i] = seq[i] + strlen(str_tmp);
			pthread_mutex_unlock(&mutx);
			i++;					// sequence number, data size(strlen method use) process
			pthread_mutex_lock(&mutx);
			seq[i] = ack[i-1];
			pthread_mutex_unlock(&mutx);
			sprintf(snd_buf, "%s## (Seq = %d, Data Size = %d)\n", str_tmp, seq[i-1], strlen(str_tmp));
			send(sock, snd_buf, strlen(snd_buf), 0);
			if(i == 5) {
				pthread_mutex_lock(&mutx);
				strcpy(time_tmp, str_tmp);		// when time-out, loss packet save at time_tmp
				pthread_mutex_unlock(&mutx);
			}
		}
	}
}


void * timeo_rcv(void * arg) {
	int sock = (int)arg;
	int i = 0;
	char tmp[BUF_SIZE] = {0};
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
		fprintf(stderr, "Error socket for recv-timeout (errno = %d)\n", errno);
	}
	pthread_mutex_lock(&mutx);
	for(i = 0; i < rcvCnt; i++) {
		if(rcvAck[i] != ack[i]) {	// if acknowledge is incorrect, packet loss
			sleep(2);
			printf("## Loss Seq : %d, we retry %d number message. ", seq[i], seq[i]);	// print to client what info loss packet
			sleep(3);
			sprintf(snd_buf, "%s## (Seq = %d, Data Size = %d)\n", time_tmp, seq[i], strlen(time_tmp));	// send to client loss packet retry
			send(sock, snd_buf, strlen(snd_buf), 0);
			rcvAck[i] = ack[i];
		}
	}
	pthread_mutex_unlock(&mutx);
	return;
}

void flow_start(void * arg) {
	int sock = (int)arg;
	int i = 0, tmp = 0, tmp_tmp = 0;
	// tmp : duplicate packet size is increased, tmp_tmp : server's size save variable
	sprintf(snd_buf, "%d", BUF_SIZE);
	sleep(1);
	send(sock, snd_buf, strlen(snd_buf), 0);
	sleep(1);
	i = recv(sock, rcv_buf, sizeof(rcv_buf), 0);
	rcv_buf[i] = '\0';
	fflush(stdout);
	printf("\n## Upper number is server's size.\n## Duplicate packet and Sending now..\n");	// 102 is server's size. it's get out from fflush
	for(i = 0; i < strlen(rcv_buf); i++) {
		if(isdigit(rcv_buf[i])) {
			tmp *= 10;
			tmp += (rcv_buf[i] - '0');
		}
	}
	tmp_tmp = tmp;
	while(1) {
		sprintf(snd_buf, "\n## Current receive Size : %d\n", tmp);
		send(sock, snd_buf, strlen(snd_buf), 0);
		tmp += tmp_tmp;
		sleep(1);
		if(tmp > BUF_SIZE) {
			sprintf(snd_buf, "\n## Last receive Size : %d, Receiving Finished!!\n", BUF_SIZE);
			send(sock, snd_buf, strlen(snd_buf), 0);
			printf("\n## Flow control Finished !!\n");
			break;
		}
	}
	return;
}

int main(int argc, char *argv[]) {
	int sock, str_len;
	struct sockaddr_in serv_adr;

	if(argc != 3) {
		printf("Usage : %s <IP> <port>\n", argv[0]);
		exit(1);
	}
	if(pthread_mutex_init(&mutx, NULL))
		perror("Mutex Init Error!");

	sock=socket(PF_INET, SOCK_STREAM, 0);
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family=AF_INET;
	serv_adr.sin_addr.s_addr=inet_addr(argv[1]);
	serv_adr.sin_port=htons(atoi(argv[2]));

	if(connect(sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
		perror("Connect() error!");

	pthread_mutex_init(&mutx, NULL);

	pthread_create(&thread, NULL, (void *)sendMessage, (void *)sock);
	pthread_create(&thread, NULL, (void *)rcvMessage, (void *)sock);
	pthread_join(thread,(void **)&sock);
	close(sock);
	return 0;
}
