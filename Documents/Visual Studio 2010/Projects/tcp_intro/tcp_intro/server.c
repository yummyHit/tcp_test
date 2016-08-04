/*
This is TCP/IP realization sample. Code write by UkjinJang.
Using Operating System is Linux; raspbian to raspberry pi.
I couldn't transfer Layer 4 to Layer 7 at 2016.05.31 (Report by Univ.)
I'll use this code for spoofing.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>

#define FILE_SIZE 10240	// File Buffer Size
#define MSS 1024	// Max Segment Size
#define rwnd MSS/10	// rwnd size is MSS/10
#define ssthresh 65536	// 64kb


int clnt_number = 0, isEnd = 0;	// client number & break variable
int clnt_sock_arr[100];		// multiple connection
int ack[MSS] = {0}, rwnd_tmp[MSS] = {0};	// acknowledge
int rcvCnt = 0, sndCnt = -1;	// send - receive count syncronized variable
int rwnd_size = rwnd, cwnd = MSS, sstv = ssthresh, max_cwnd = 102400;	// rwnd & cwnd & ssthresh & max_cwnd variable
int cgst_cnt = 0;				// Congestion control variable(when use avoid)
char snd_buf[MSS], rcv_buf[MSS],  file_buffer[FILE_SIZE];	// message & file transfer string
char name[7] = "Server";
pthread_mutex_t mutx;		// thread <-> stack flexible variable
pthread_t thread[100];		// thread number(user count)
struct timeval timeout;		// when didn't accept client, timeout!

void flow_start(char * msg, void * arg);	// flow control start method
int slow_start(void * arg);					// slow start in congestion control
void cgst_avoid(void * arg);				// congestion avoid in congestion control
void recovery(void * arg);					// fast recovery in congestion control

void * sendMessage(void * arg) {		// send message in chat from thread
	int sock = (int)arg;				// socket variable(void * -> int)
	timeout.tv_sec = 5;					// timeout 5sec
	timeout.tv_usec = 0;
	char tmp[333];	// chat buffer size 333
	while(1) {
		printf("\n## %s >> ", name);		// Server >>
		fgets(tmp, 333, stdin);				// scanf
		tmp[strlen(tmp) -1] = '\0';			// end of content, close array
		if(!strncmp(tmp, "quit", 4)) {		// if server enter to quit,
			sprintf(snd_buf, "\n## Quit\n## %s log out", name);
			send(sock, snd_buf, strlen(snd_buf), 0);	// send to client :Quit ## server log out
			close(sock);
			break;			// thread finish
		}
		else {								// if server don't enter to quit
			sndCnt += 1;
			if(rcvCnt == sndCnt) {			// receive message check
				printf("\n## Message isn't exist yet. Wait client Message...\n");
				sndCnt -= 1;				// didn't exist message. server can't give acknowledge.
				continue;
			}
			else if(sndCnt == 4) {			// intentionally give timeout to client
				sprintf(snd_buf, "request");
				send(sock, snd_buf, strlen(snd_buf), 0);
			}
			else {						// commonly chat + give acknowlege
				sprintf(snd_buf, "## %s >> %s\n## (Accept Acknowledge = %d)\n", name, tmp, ack[sndCnt]);
				send(sock, snd_buf, strlen(snd_buf), 0);
				pthread_mutex_lock(&mutx);
				rwnd_size += rwnd_tmp[sndCnt];		// if send message, rwnd size increase by data size
				pthread_mutex_unlock(&mutx);
			}
		}
	}
}

void * rcvMessage(void * arg) {		// receive message in chat from thread
	int sock = (int)arg;			// client socket variable
	int seq_tmp = 0, data_tmp = 0;	// sequence number, data_size variable
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	FILE *fp;						// file transfer pointer variable
	int i = 0, j = 0, cnt = 0;		// normal variable(i = recv, j = for loop, cnt = acknowledge size
	while(1) {
		if((i = recv(sock, rcv_buf, sizeof(rcv_buf), 0)) > 0) {	// recv message(i = 1024)
			rcv_buf[i] = '\0';			// last message buffer, close array
			fflush(stdout);				// if buffer value exist, print it
			if(cnt > 0) {
				if(ack[cnt-1] > rwnd_size) {	// if rwnd is full, don't receive message
					sprintf(snd_buf, "oversize");
					send(sock, snd_buf, strlen(snd_buf), 0);
					continue;
				}
			}
			pthread_mutex_lock(&mutx);	// rcvCnt is public variable in stack. so using lock&unlock
			rcvCnt += 1;
			pthread_mutex_unlock(&mutx);
			if(!strncmp(rcv_buf, "file", 4)) {	// if recv message is 'file'
				printf("\n## Client wants file transport...\n");
				i = recv(sock, rcv_buf, sizeof(rcv_buf), 0);	// one more receive message
				rcv_buf[i] = '\0';
				fflush(stdout);
				if((fp = fopen(rcv_buf, "r")) == NULL) {	// and second message check
					printf("## File Not Found!!\n");
					sprintf(snd_buf, "failed");
					send(sock, snd_buf, strlen(snd_buf), 0);
					fp = NULL;
					continue;
				}
				else {									// if second message is correct file name,
					printf("## File Found!! Transporting ...\n");
					while(!feof(fp)) {
						fgets(rcv_buf,1024,fp);
						if(feof(fp))
							break;
						strcat(file_buffer,rcv_buf);	// file open and read each sentence, and append it 'file_buffer'
					}
					fclose(fp);
					sprintf(snd_buf, "success");
					send(sock, snd_buf, strlen(snd_buf), 0);	// signal "file transfer success"
					sleep(3);
					send(sock, file_buffer, strlen(file_buffer), 0);	// give to client file content
					sleep(3);
					printf("\n## Server >> File IO is finish. New client waiting...\n");
					printf("#####################################################\n");
					fp = NULL;
					continue;
				}
			}
			else if(!strncmp(rcv_buf, "flowStart", 9)) {	// if client message is 'flowStart'
				pthread_mutex_lock(&mutx);
				rcvCnt -= 1;				// it isn't seq-data & ack relation, so rcvCnt value - 1
				pthread_mutex_unlock(&mutx);
				printf("\n## Client wants flow control...\n");
				i = recv(sock, rcv_buf, sizeof(rcv_buf), 0);	// recv client size
				rcv_buf[i] = '\0';
				fflush(stdout);
				printf("## server's size is 102, client's size is %s.\n## starting flow control !!\n", rcv_buf);
				sleep(1);
				flow_start(rcv_buf, (void *)sock);
			}
			else if(!strncmp(rcv_buf, "cgstStart", 9)) {	// if client message is 'cgstStart'
				printf("\n## Client wants congestion control...\n");
				while(1) {
					i = slow_start((void *)sock);	// slow start !!
					if(i != 0) {		// i variable is number when cwnd >= ssthresh, break
						printf("\n## Slow Start Finished. Congestion Avoid Start..\n");
						while(1) {
							cgst_avoid((void *)sock);
							if(cgst_cnt >= 3)	// in cgst_avoid method, cgst_cnt value + 1 when 1/10 MSS * 10 times run
								break;
						}
						printf("\n## Congestion Avoid Finished. Recovery Start...\n");
						pthread_mutex_lock(&mutx);
						rcvCnt -= 1;
						cgst_cnt = 0;
						pthread_mutex_unlock(&mutx);
						while(1) {
							recovery((void *)sock);	// fast recovery run!!
							if(cgst_cnt >= 3)
								break;
						}
					}
					i = 0;
					if(cgst_cnt >= 3)
						break;
				}
			}
			else if(!strncmp(rcv_buf, "quit", 4)) {	// if receive message 'quit'
				pthread_mutex_lock(&mutx);
				clnt_number--;
				pthread_mutex_unlock(&mutx);
				printf("\n%s", rcv_buf);
				close(sock);		// client socket closed
			}
			else {
				j = MSS;		// give enough size to j value
				for(i = 0; i < strlen(rcv_buf); i++) {	// client send (abcdefasdcasd sequence number = XX data size = XX)
					if(!isdigit(rcv_buf[i]) && rcv_buf[i] == '=') {	// if not digit and correct = signal
						for(j = i+1; j < strlen(rcv_buf); j++) {	// give next to = size to j value
							if(!isdigit(rcv_buf[j]) && rcv_buf[j] == '=') {
								i = j+1;		// secondly correct = signal, give next to = size to i value, break
								break;
							}
							else if(isdigit(rcv_buf[j])) {	// after first = signal, sequence number save to seq_tmp variable
								seq_tmp *= 10;
								seq_tmp += (rcv_buf[j] - '0');
							}
						}
					}
					if(isdigit(rcv_buf[i]) && i > j) {		// after second = signal, data size save to data_tmp variable
						data_tmp *= 10;
						data_tmp += (rcv_buf[i] - '0');
					}
				}
				pthread_mutex_lock(&mutx);
				ack[cnt] = seq_tmp + data_tmp;	// acknowledge save(ack is public variable in stack)
				rwnd_tmp[cnt] = data_tmp;	// for increase rwnd threshold
				pthread_mutex_unlock(&mutx);
				cnt++;
				seq_tmp = 0;
				data_tmp = 0;
				printf("\n%s", rcv_buf);
			}
		}
	}
}

void * serverThread(void * arg) {		// always running
	int sock = (int)arg;
	int i;
	while(1) {
		if(recv(sock, rcv_buf, sizeof(rcv_buf), 0) > 0) {
			rcv_buf[MSS] = '\0';
			printf("\n%s", rcv_buf);
			pthread_mutex_lock(&mutx);
			for(i = 0; i < clnt_number; i++)
				if(clnt_sock_arr[i] != sock)
					send(clnt_sock_arr[i], rcv_buf, strlen(rcv_buf), 0);
			pthread_mutex_unlock(&mutx);
			pthread_create(&thread[clnt_number], NULL, rcvMessage, (void *)sock);	// receive message thread run
			pthread_create(&thread[clnt_number], NULL, sendMessage, (void *)sock);	// send message thread run
			pthread_join(thread[clnt_number], (void **)&sock);						// when two thread finish, end of buffer in thread
		}
	}
}

void flow_start(char * msg, void * arg) {
	int sock = (int)arg;
	int size = 102;		// it is server size
	int i = 0, tmp = 0, rcv_tmp = 0;
	for(i = 0; i < strlen(msg); i++) {
		if(isdigit(msg[i])) {	// get the client's size
			tmp *= 10;
			tmp += (msg[i] - '0');
		}
	}
	if(tmp > size) {	// if client's size > server's size
		sprintf(snd_buf, "%d", size);
		send(sock, snd_buf, strlen(snd_buf), 0);	// send to client server's size
	}
	sleep(1);
	while(1) {
		i = recv(sock, rcv_buf, sizeof(rcv_buf), 0);
		rcv_buf[i] = '\0';
		fflush(stdout);
		for(i = 0; i < strlen(rcv_buf); i++) {	// receive duplicate size to client
			if(isdigit(rcv_buf[i])) {
				rcv_tmp *= 10;
				rcv_tmp += (rcv_buf[i] - '0');
			}
		}
		printf("%s", rcv_buf);
		if(rcv_tmp >= tmp) 
			break;
		rcv_tmp = 0;
	}
	return;
}

int slow_start(void * arg) {
	int sock = (int)arg;
	int i;		// it is return value
	char tmp[333];	// send buffer
	sprintf(tmp, "## Cwnd Value : %d\n", cwnd);	// current cwnd value send to client
	for(i = 0; i < cwnd; i++) 
		send(sock, tmp, strlen(tmp), 0);	// send to client for cwnd size
	i = 0;
	printf("## You send msg / Cwnd Value : %d\n", cwnd);	// can check at server
	sleep(2);
	cwnd += cwnd;		// cwnd increase 2 times
	if(cwnd >= sstv) {
		sstv = cwnd / 2;		// slow start is follow this algorithm.
		i = 100;
	}
	return i;
}

void cgst_avoid(void * arg) {
	int sock = (int)arg;
	int i = 0;	// only for loop variable
	char tmp[333];	// send buffer
	for(i = 0; i < 10; i++) {	// 1/10 MSS increase at 3 times
		sprintf(tmp, "## Cwnd Value : %d\n", cwnd);
		sleep(1);
		send(sock, tmp, strlen(tmp), 0);
		printf("## You send msg / Cwnd Value : %d\n", cwnd);
		cwnd += (MSS/10);
	}
	if(cwnd >= max_cwnd) {	
		// if cwnd more than max_cwnd, threshold is half cwnd value and cwnd is start from threshold value
		sstv = cwnd / 2;
		cwnd = sstv;
		cgst_cnt = 3;
		return;
	}
	cgst_cnt++;	// cgst_avoid stop to cgst_cnt is 3 in send thread
	if(cgst_cnt >= 3) {
		sstv = cwnd / 2;
		cwnd = sstv;
	}
}

void recovery(void * arg) {
	int sock = (int)arg;
	char tmp[333];
	sprintf(tmp, "## Cwnd Value : %d\n", cwnd);
	send(sock, tmp, strlen(tmp), 0);
	printf("## You send msg / Cwnd Value : %d\n", cwnd);
	cwnd += MSS;
	if(cwnd >= max_cwnd) {	// cwnd increase at max_cwnd, and restart recovery.
		printf("\n## cwnd size reach max_cwnd size !!\n");
		sstv = cwnd/2;
		cwnd = sstv;
		cgst_cnt += 1;
	}
	sleep(1);
}

int main(int argc, char **argv) {
	int serv_sock, clnt_sock, clnt_addr_size;
	struct sockaddr_in serv_addr;
	struct sockaddr_in clnt_addr;
	if(argc != 2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);		// ./server port : linux server execute way
	}
	if(pthread_mutex_init(&mutx, NULL))
		perror("Mutex Init Error!");	// thread error

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

	if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		perror("Bind() Error!");
	if(listen(serv_sock, 5) == -1)
		perror("Listen() Error!");

	printf("Welcome to Chatting Server.\n");
	printf("LISTENING...\n");
	printf("#####################################################\n");
	pthread_mutex_init(&mutx, NULL);		// thread start
	clnt_addr_size = sizeof(clnt_addr);
	while(1) {
		pthread_mutex_lock(&mutx);
		clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
		pthread_mutex_unlock(&mutx);
		if(clnt_sock == -1)
			perror("Accept() error!");
		else {
			pthread_mutex_lock(&mutx);
			clnt_sock_arr[clnt_number] = clnt_sock;
			pthread_mutex_unlock(&mutx);
			pthread_create(&thread[clnt_number], NULL, serverThread, (void *)clnt_sock);
			printf("## New Connect, Client IP : %s\n", inet_ntoa(clnt_addr.sin_addr));
			pthread_join(thread[clnt_number],(void**)&clnt_sock);
		}
		pthread_mutex_lock(&mutx);
		clnt_number++;
		pthread_mutex_unlock(&mutx);
	}
	pthread_destroy(&mutx);	// thread finish
	close(clnt_sock);
	return 0;
}