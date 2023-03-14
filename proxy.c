#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void doit(int fd);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_url(char *url, char *port, char *servername, char *filename);
int forward_request(rio_t *rio, char *servername, char *port, char *filename);	
int forward_response(int client_fd, int server_fd);

int main(int argc, char *argv[])
{
	int listen_fd, client_fd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t client_len;
	struct sockaddr_storage client_addr;	

	if (argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	
	listen_fd = Open_listenfd(argv[1]);
	while(1)
	{
		client_len = sizeof(client_addr);
		client_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);
		Getnameinfo( (SA *)&client_addr, client_len, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		doit(client_fd);
		Close(client_fd);
	}	
    return 0;
}

void doit(int client_fd)
{
	char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], servername[MAXLINE], port[6];
	int server_fd;
	
	rio_t client_rio;
	
	Rio_readinitb(&client_rio, client_fd);
	Rio_readlineb(&client_rio, buf, MAXLINE);

	//printf("%s", buf);
	sscanf(buf, "%s %s %s", method, url, version);
	if (strcasecmp(method, "GET"))
	{
		client_error(client_fd, method, "501", "Not implemented", "The proxy does not implement the method");
		return;
	}
	
	if ( parse_url(url, port, servername, filename) == -1)
	{
		client_error(client_fd, method, "400", "Bad Request", "The proxy can't parse the url");
		return;	
	}
	
	server_fd = forward_request(&client_rio, servername, port, filename);	
	//printf("\nhas forwarded the request\m");
	if ( server_fd == -1)
	{
		client_error(client_fd, method, "400", "Bad Request", "The proxy can't access the server");
		return;
	}
	forward_response( client_fd, server_fd);
	//printf("\nhas forwarded the response\m");
	Close(server_fd);
	
}

int parse_url(char *url, char *port, char *servername, char *filename)
{
	char *ptr;
	char tmp_service[MAXLINE] = {0};
	char tmp_filename[MAXLINE] = {0};
	char tmp_servername[MAXLINE] = {0};

	strcpy(filename, "/");
	sscanf(url, "%[^:/]://%[^/]/%s", tmp_service, tmp_servername, tmp_filename);
	if ( strlen(tmp_filename))
		strcat(filename, tmp_filename);
	if ( !strlen(tmp_servername) )
		return -1;
	if ( (ptr=strchr(tmp_servername, ':')) )
	{
		ptr[0] = '\0';
		ptr += 1;
		if ( strlen(ptr) )
			strcpy(port, ptr);
		else 
			strcpy(port, tmp_service);
		strcpy(servername, tmp_servername);
	}	
	else
	{
		strcpy(port, tmp_service);
		strcpy(servername, tmp_servername);
	}
	return 0;	
	
}

void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];
	
	sprintf(body, "<html><title>Proxy Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Proxy Server</em>\r\n", body);

	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));

}

int forward_request(rio_t *read_rio, char *servername, char* port, char *filename)
{
	char buf[MAXLINE];
	int server_fd;
	
	server_fd = Open_clientfd(servername, port);
	if (server_fd <0)
		return -1;	
	sprintf(buf, "GET %s HTTP/1.0\r\n", filename);
	Rio_writen(server_fd, buf, strlen(buf));

	sprintf(buf, "Host: %s\r\n", servername);
	Rio_writen(server_fd, buf, strlen(buf));

	Rio_writen(server_fd,(void*) user_agent_hdr, strlen(user_agent_hdr));

	sprintf(buf, "Connection: close\r\n");
	Rio_writen(server_fd, buf, strlen(buf));

	sprintf(buf, "Proxy-Connection: close\r\n");
	Rio_writen(server_fd, buf, strlen(buf));

	while(1)
	{
		//printf("\n looping in forward_request \n");
		Rio_readlineb(read_rio, buf, MAXLINE);
		//printf("%s", buf);
		if ( (!strstr(buf, "Host:")) && (!strstr(buf, "User-Agent:")) 
			&& (!strstr(buf, "Connection:")) && (!strstr(buf, "Proxy-Connection:")) )
		{
			Rio_writen(server_fd, buf, strlen(buf));
		}
		if ( strcmp(buf, "\r\n") == 0)
		{
			//printf("it should stop now");
			break;
		}
	}	
	//printf("ready to return");
	return server_fd;
}



int forward_response(int client_fd, int server_fd)
{
	char buf[MAXLINE];
	rio_t server_rio;
	int num;
	
	Rio_readinitb(&server_rio, server_fd);

	while(1)
	{
		if ((num = Rio_readnb(&server_rio, buf, MAXLINE)) == 0)
			break;
		Rio_writen(client_fd, buf, num);	
		//printf("\n forwarding %d bytes to client\n", num);
	}	
	return 0;
}
