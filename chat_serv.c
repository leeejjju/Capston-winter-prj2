#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include "cJSON.h"

#define BUF_SIZE 256
#define MAX_USER 4
#define NAME_SIZE 16

int start_flag = 0;
int end_flag = 0;
int max_user;
char ** user_name;
int json_size;
char * json_serialize;

int clnt_cnt = 0;
int clnt_socks[MAX_USER];
pthread_mutex_t mutx;


int roadJson() 
{
	
	char filepath[256];
	fgets(filepath, 256, stdin);
	filepath[strlen(filepath)-1]=0;
	FILE *file = fopen(filepath,"r");
	if(file == NULL){
	fprintf(stderr,"ERROR: open file");
		return 1;
	}
	struct stat st;
	if(stat(filepath, &st) == -1){
  		fprintf(stderr,"ERROR: stat()\n");
  	return 1;
	}
	int size = st.st_size;

	char* jsonfile = (char*)malloc(size+1);
	if(jsonfile == NULL){
		fprintf(stderr,"ERROR: memory allocation\n");
		return 1;
	}

	int read_size = fread(jsonfile, 1, size, file);
	if(read_size != size){
		fprintf(stderr, "ERROR: read file\n");
		return 1;
	}

	fclose(file);
	jsonfile[size] = '\0';
	
	cJSON* root = cJSON_Parse(jsonfile);
	if (root == NULL) {
			printf("JSON 파싱 오류: %s\n", cJSON_GetErrorPtr());
      return 1;
	}

	cJSON* num_user = cJSON_GetObjectItem(root, "max_user");
	max_user = num_user->valueint;
	// max_user = 1;
	user_name = (char**)malloc(sizeof(char*) * max_user);
	for(int i=0; i< max_user; i++)
	{
		user_name[i] = (char*)malloc(sizeof(char) * NAME_SIZE);
	}

	json_serialize = cJSON_Print(root);
	json_size = strlen(json_serialize);
	
	free(root);
	free(jsonfile);
	return 0;
}

void disconnected(int sock)
{
	pthread_mutex_lock(&mutx);
	for (int i = 0; i < clnt_cnt; i++)   // remove disconnected client
	{
		if (sock == clnt_socks[i])
		{
			while (i < clnt_cnt-1)
			{
				clnt_socks[i] = clnt_socks[i+1];
				i++;
			}
			break;
		}
	}
	clnt_cnt--;
	if(clnt_cnt == 0)
		end_flag = 0;
	pthread_mutex_unlock(&mutx);
	close(sock);
}


int write_byte(int sock, void * buf, int size){

	int write_size = 0;
	int str_len = 0;
	while(write_size < size)
	{
		str_len = write(sock, buf + write_size, size - write_size);
		if( str_len == 0)
		{
			return 0;
		}
		if( str_len == -1)
		{
			disconnected(sock);
		}
		write_size += str_len;
	}
	return write_size;
}

int read_byte(int sock, void * buf, int size)
{
	int read_size = 0;
	int str_len = 0;
	while(read_size < size)
	{
		str_len = read(sock, buf + read_size, size - read_size);
		if( str_len == 0)
		{
			disconnected(sock);
			return 0;
		}
		if( str_len == -1)
		{

		}
		read_size += str_len;
	}
	return read_size;
}//상훈님 파이팅 

void send_msg_all(void * event, int len)   // send to all
{
	// printf("%d\n",*(int *)event);
	pthread_mutex_lock(&mutx);
	for (int i = 0; i < clnt_cnt; i++)
	{
		write_byte(clnt_socks[i], event, len);
	}
	pthread_mutex_unlock(&mutx);
}

void *handle_clnt(void * arg)
{
	int clnt_sock = *((int*)arg);
	int str_len = 0;
	int event;
	int name_size = 0;

	//recive name size
	str_len = read_byte(clnt_sock, (void *)&name_size, sizeof(int));
	//reciev name, send id
	pthread_mutex_lock(&mutx);
	for (int i = 0; i < clnt_cnt; i++) 
	{
		if (clnt_sock == clnt_socks[i])
		{
			// printf("id : %d\n", i);
			read_byte(clnt_sock, (void *)user_name[i], name_size);
			printf("%s is enter\n",user_name[i]);
			write_byte(clnt_sock, (void *)&i, sizeof(int));
			start_flag++;
			break;
		}
	}
	pthread_mutex_unlock(&mutx);

	//send json
	write_byte(clnt_sock, (void *)&json_size, sizeof(int));
	write_byte(clnt_sock, json_serialize, json_size);

			
	while(start_flag < max_user);
	end_flag = 1;
	
	
	for(int i=0; i< max_user; i++)
	{
		int len = strlen(user_name[i]);
		write_byte(clnt_sock, &len,sizeof(int));
		write_byte(clnt_sock,user_name[i], len);
	}

	//event echo
	while (read_byte(clnt_sock, (void *)&event, sizeof(int))) 
	{
		printf("move: %d\n", event);
		send_msg_all((void *)&event, sizeof(int));
		if(event == 16)
		{
			printf("end game!\n");
			disconnected(clnt_sock);
		}
	}
	
	
	return NULL;
}



void error_handling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	unsigned int clnt_adr_sz;
	pthread_t t_id;
	if (argc != 2) {
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
	
	if(roadJson())
		exit(1);

	#ifdef DEBUG
		fprintf(stderr,"max user : %d\n", max_user);
	#endif

	pthread_mutex_init(&mutx, NULL);
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET; 
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(argv[1]));
	
	if (bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");
	if (listen(serv_sock, 5) == -1)
		error_handling("listen() error");
	
	//todo user max될때 처리
	while (1)
	{
		pthread_mutex_lock(&mutx); 
		while(end_flag);
		pthread_mutex_unlock(&mutx);
		
		clnt_adr_sz = sizeof(clnt_adr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
		
		pthread_mutex_lock(&mutx); 
		clnt_socks[clnt_cnt++] = clnt_sock;
		pthread_mutex_unlock(&mutx);
		
		pthread_create(&t_id, NULL, handle_clnt, (void*)&clnt_sock);
		pthread_detach(t_id);

		
	}
	free(json_serialize);
	close(serv_sock);
	return 0;
}
	