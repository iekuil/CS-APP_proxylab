#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_node{
	char url[MAXLINE];
	char cache[MAX_OBJECT_SIZE];
	int object_size;
	struct cache_node *nextp;
}cnode;
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
cnode *cache_entry = NULL;
int cache_size = 0;
int readcnt = 0;
sem_t mutex, w;

void doit(int fd);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_url(char *url, char *port, char *servername, char *filename);
int forward_request(rio_t *rio, char *servername, char *port, char *filename);	
int forward_response(int client_fd, int server_fd, char *url);
void *thread(int client_fd);

void init_cnode(cnode *node, char *url);
void insert_cnode(cnode *node);
cnode *remove_cnode(cnode *node);
void excile_tail();

cnode *search_cache(char *url);
int forward_cache(int client_fd, cnode *object_cache);
cnode* in_cache(char *url);
int set_first(cnode *node);
void copy_to_cache(cnode *node, char *buf, int num);
void add_or_drop(cnode *node);

void check_cache();


int main(int argc, char *argv[])
{
	int listen_fd, client_fd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t client_len;
	struct sockaddr_storage client_addr;	
	pthread_t tid;

	if (argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	
	
	Sem_init(&mutex, 0, 1);
	Sem_init(&w, 0, 1);
	listen_fd = Open_listenfd(argv[1]);
	while(1)
	{
		client_len = sizeof(client_addr);
		client_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);
		Getnameinfo( (SA *)&client_addr, client_len, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		//doit(client_fd);
		//Close(client_fd);
		Pthread_create(&tid, NULL, thread, client_fd);
	}	
    return 0;
}

void doit(int client_fd)
{
	char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], servername[MAXLINE], port[6];
	int server_fd;
	cnode *object_cache;
	
	rio_t client_rio;
	
	Rio_readinitb(&client_rio, client_fd);
	Rio_readlineb(&client_rio, buf, MAXLINE);

	sscanf(buf, "%s %s %s", method, url, version);
	if (strcasecmp(method, "GET"))
	{
		client_error(client_fd, method, "501", "Not implemented", "The proxy does not implement the method");
		return;
	}

	object_cache = in_cache(url);
	if ( object_cache )
	{
		forward_cache(client_fd, object_cache);
		set_first(object_cache);	
		return;
	}
	
	if ( parse_url(url, port, servername, filename) == -1)
	{
		client_error(client_fd, method, "400", "Bad Request", "The proxy can't parse the url");
		return;	
	}
	
	server_fd = forward_request(&client_rio, servername, port, filename);	

	if ( server_fd == -1)
	{
		client_error(client_fd, method, "400", "Bad Request", "The proxy can't access the server");
		return;
	}
	forward_response( client_fd, server_fd, url);
	Close(server_fd);
	//check_cache();
	
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
		Rio_readlineb(read_rio, buf, MAXLINE);
		if ( (!strstr(buf, "Host:")) && (!strstr(buf, "User-Agent:")) 
			&& (!strstr(buf, "Connection:")) && (!strstr(buf, "Proxy-Connection:")) )
		{
			Rio_writen(server_fd, buf, strlen(buf));
		}
		if ( strcmp(buf, "\r\n") == 0)
		{
			break;
		}
	}	
	return server_fd;
}



int forward_response(int client_fd, int server_fd, char *url)
{
	char buf[MAXLINE];
	rio_t server_rio;
	int num;
	
	cnode *object_cache = Malloc(sizeof(cnode));
	init_cnode(object_cache, url);
	
	Rio_readinitb(&server_rio, server_fd);

	while(1)
	{
		printf("looping, in forward_response\n");
		if ((num = Rio_readnb(&server_rio, buf, MAXLINE)) == 0)
			break;
		copy_to_cache(object_cache, buf, num);
		Rio_writen(client_fd, buf, num);	
	}	
	printf("loop ends, in forward_response\n");
	add_or_drop(object_cache);
	return 0;
}

void *thread(int client_fd)
{
	Pthread_detach(Pthread_self());
	doit(client_fd);
	Close(client_fd);
	return NULL;
}


void insert_cnode(cnode *node)
{
	if ( cache_entry == NULL )
	{
		cache_entry = node;
		node->nextp = NULL;
		cache_size = cache_size + node->object_size;
		return ;
	}
	cnode *origin_first = cache_entry;
	cache_entry = node;
	node->nextp = origin_first;
	cache_size = cache_size + node->object_size;

	while(cache_size > MAX_CACHE_SIZE )
	{
		excile_tail();
	}
	return ;
}

cnode *remove_cnode(cnode *node)
{
	cnode *nodep, *prevp;
	prevp = cache_entry;
	nodep = prevp->nextp;
	
	while ( nodep )
	{
		if ( node == nodep)
		{
			prevp->nextp = nodep->nextp;
			nodep->nextp = NULL;
			cache_size = cache_size - nodep->object_size;
			break;	
		}
		nodep = nodep->nextp;
		prevp = prevp->nextp;
	}
	return nodep;
}

void excile_tail()
{
	cnode *nodep, *prevp;
	prevp = cache_entry;
	if (prevp == NULL)
		return;
	nodep = prevp->nextp;
	while ( nodep->nextp )
	{
		nodep = nodep->nextp;
		prevp = prevp->nextp;
	}
	cache_size = cache_size - nodep->object_size;
	Free(nodep);
	prevp->nextp = NULL;
}

cnode *search_cache(char *url)
{
	cnode *nodep;
	nodep = cache_entry;
	while ( nodep )
	{
		if ( !strcmp(url, nodep->url) )
			break;
		nodep = nodep->nextp;
	}
	return nodep;
}



void init_cnode(cnode *node, char *url)
{
	strcpy(node->url, url);
	node->object_size = 0;
	node->nextp = NULL;
	return;	
}



cnode *in_cache(char *url)
{
	cnode *node;

	P(&mutex);
	readcnt++;
	if (readcnt == 1)
		P(&w);
	V(&mutex);

	node = search_cache(url);

	P(&mutex);
	readcnt--;
	if (readcnt == 0)
		V(&w);
	V(&mutex);

	return node;
}

int forward_cache(int client_fd, cnode *object_cache)
{
	P(&mutex);
	readcnt++;
	if (readcnt == 1)
		P(&w);
	V(&mutex);

	Rio_writen(client_fd, object_cache->cache, object_cache->object_size);

	P(&mutex);
	readcnt--;
	if (readcnt == 0)
		V(&w);
	V(&mutex);	
	return 0;
}

int set_first(cnode *node)
{
	P(&w);
	remove_cnode(node);
	insert_cnode(node);
	V(&w);
	return 0;
}

void copy_to_cache(cnode *node, char *buf, int num)
{
	if ( node->object_size + num <= MAX_OBJECT_SIZE)
	{
		char *ptr = node->cache + node->object_size;
		memcpy(ptr, buf, num);
	}
	node->object_size += num;
	return ;
}

void add_or_drop(cnode *node)
{
	if ( node->object_size < MAX_OBJECT_SIZE)
	{
		P(&w);
		insert_cnode(node);
		V(&w);
		return;
	}
	Free(node);
	return;
}

void check_cache()
{
	P(&mutex);
	readcnt++;
	if (readcnt == 1)
		P(&w);
	V(&mutex);

	cnode *p;
	p = cache_entry;
	int index = 1;
	while(p)
	{
		printf("No.%d, size:%d, url:%s\n", index++, p->object_size, p->url);
		p = p->nextp;
	}
	if ( index == 1)
		printf("no cache\n");
	
	P(&mutex);
	readcnt--;
	if (readcnt == 0)
		V(&w);
	V(&mutex);
}
