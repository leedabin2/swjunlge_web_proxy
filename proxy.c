#include <stdio.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 프록시 캐시
typedef struct cache_node
{
  char *file_path; // 웹 객체 path
  int content_length;
  char *response;
  struct cache_node *prev, *next;
} cache_node;

typedef struct cache 
{
  cache_node *root;
  cache_node *tail;
  int total_size; // 캐싱된 객체 총 사이즈
} cache;

struct cache *p; // root, tail을 전역으로 선언

cache_node *insert_cache(cache *p, char *path, char *response_p, int content_length);
cache_node *find_cache(cache *p, char *path);
void move_cache(cache_node *node, cache *p); 
void delete_cache(cache *p);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void read_request_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
void parse_uri(char *uri, char *hostname, char *path, int *port);
int connect_endserver(char *hostname,int port);

void *thread(void *vargp);

// 클라이언트로 요청이 들어오면 새로운 연결을 생성 후 원격 서버에 해당 요청을 전달
int main(int argc, char **argv) {

  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];  // 프록시가 요청을 받고 응답해줄 클라이언트의 IP, Port
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; 
  pthread_t tid; // thread ID 를 저장하기 위함

  p = (cache *)malloc(sizeof(cache));
  p->root = NULL;
  p->tail = NULL;
  p->total_size = 0;

  if (argc != 2) {  // 명령줄 인수의 수가 맞지 않으면
    fprintf(stderr, "usage: %s <port>\n", argv[0]);  // 프로그램이름과, 포트 번호를 지정하라는 메시
    exit(1); 
  }

  listenfd = Open_listenfd(argv[1]); // 듣기 소켓을 오픈 (해당 포트 번호에 해당하는)

  while (1) { // 클라이언트한테 받은 연결 요청을 accept (무한 루프)
    clientlen = sizeof(struct sockaddr_storage);
    connfd = (int*)Malloc(sizeof(int)); // 여러개의 식별자를 만들기 위해서 덮어쓰지 못하게 고유 메모리를 생성
    // clientfd, connfd 사이의 연결을 수립
    *connfd = Accept(listenfd, (SA *)&clientaddr, // 듣기식별자에 도달하기를 기다리고, 클라이언트 소켓 주소를 채우고, 연결 식별자를 리턴
                    &clientlen);  // line:netp:tiny:accept 반복적으로 연결 요청을 접수
    
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0); // hostname, port 반환 -> 서버가 클라이언트의 주소 정보를 알아내는 용도
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid,NULL,thread,connfd);
  }
  return 0;
}

void *thread(void *vargp) {
  int connfd = *((int *)vargp); // 피어쓰레드가 이 포인터를 역참조
  Pthread_detach(pthread_self()); // 피어쓰레드는 요청을 처리하기 전에 자신을 분리 (다른 쓰레드에 의해서 청소되거나 종료될 수 없기 때문) 
  Free(vargp); // 자신의 메모리 자원들이 종료 후 반환처리
  doit(connfd);
  Close(connfd);
  return NULL;
}

// 연결 리스트 생성
cache_node *insert_cache(cache *p,char *path, char *response_p, int content_length) {
  // 새 노드 생성
  cache_node *newnode = (cache_node *)malloc(sizeof(cache_node));

  newnode->response = response_p;
  newnode->file_path = path;
  newnode->content_length = content_length;
  newnode->prev = NULL;
  newnode->next = NULL;

  if (content_length > MAX_OBJECT_SIZE) 
    return;
  // 만약 들어오는 값 + totalsize 가 최대 캐시 크기 이상이면 delete
  while (newnode->content_length + p->total_size > MAX_CACHE_SIZE)
  {
    delete_cache(p->tail);
  } // 아니면 root부터 넣어주기 
  if (p->root == NULL) {
    p->root = newnode;
    p->tail = newnode;
  } else {
    p->root->prev = newnode;
    newnode->next = p->root;
    p->root = newnode;
  }
  p->total_size += newnode->content_length;
}

// 캐시를 조회해서 path를 지닌 캐시를 찾음
cache_node *find_cache(cache *p, char *path) {
  // 루트 노드에서 부터 찾아나감
  cache_node *search = p->root;
  for (search; search!=NULL; search = search->next) {
    if (strcmp(search->file_path,path) == 0 )
      return search;
  }
  // 없으면 null
  return NULL;
};

// 찾은 노드를 변수로 받고, 맨 앞 노드로 이동
void move_cache(cache_node *node, cache *p) {
  // 루트노드의 path가 찾는 거랑 같으면 루트노드기 때문에 변경사항 없음
  if (p->root == node){
    
  }
  // 마지막 노드가 가지고 있는 path가 내가 찾는 path와 같은경우
  else if(p->tail == node){
    p->tail->prev->next = NULL;
    p->tail = node->prev;
    node->next = p->root;
    node->prev = NULL;
    p->root->prev = node;
    p->root = node;
    
  }
  // 링크드 리스트 중간의 노드가 같은경우
  else{
    node->next->prev = node->prev;
    node->prev->next = node->next;
    node->next = p->root;
    p->root->prev = node;
    node->prev = NULL;
    p->root = node;
  }
}

// 뒤에서 부터 삭제하기
void delete_cache(cache *p) {
  cache_node *search;

  search = p->tail;
  p->tail = p->tail->prev;
  search->prev->next = NULL; // 연결끊기
  p->total_size -= search->content_length;
  free(search);
}

// 클라이언트의 연결요청이 유효하면, 웹서버에 연결 설정
void doit(int connfd) {

  int end_serverfd; // 연결할 웹서버용 fd 생성
  int port;
  char client_buf[MAXLINE], server_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 클라이언트로부터 받은 요청라인을 저장
  char hostname[MAXLINE],path[MAXLINE];
  char endserver_http_header[MAXLINE]; // 서버에 보낼 요청 헤더 생성

  rio_t rio, server_rio; // 클라용 rio, 서버용 rio (네트워크 통신을 위함)

  cache_node *node;
  char *client_path = (char *)malloc(MAXLINE);
  char *response_buf = (char *)malloc(MAX_OBJECT_SIZE);

  // 클라이언트 요청 읽기(클라와의 fd를 클라용 rio에 연결)
  Rio_readinitb(&rio,connfd); 
  Rio_readlineb(&rio,client_buf,MAXLINE);
  printf(" Client Request headers: \n"); 
  printf("%s",client_buf);
  sscanf(client_buf,"%s %s %s", method, uri, version);  // 요청라인에서 메소드, uri, version 분리해서 저장

  // HTTP 요청 검사 - GET이 아니면 에러 처리
  if (!(strcasecmp(method,"GET") == 0)) { 
    printf("Server provide only GET method");
    // 추후 클라이언트 에러함수로 수정
    return;
  }

  // uri를 분석해서 파싱하기(uri,호스트네임,목적지호스트, 포트)
  parse_uri(uri,hostname,path, &port); 

  strcpy(client_path,path);
  // 클라이언트 요청에서 캐싱이 된 건지 확인하기
  node = find_cache(p,client_path);
  // 캐싱된 객체면 클라이언트에게 바로 전송하기
  if (node != NULL) {
    move_cache(node,p); // 사용된 거 move_cache
    Rio_writen(connfd,node->response,node->content_length);
    return;
  }
  
  // 캐싱되지 않은 객체면 
  // 서버로 요청을 보냄 
  // 서버의 응답을 클라이언트에게 보내기 전에 캐시에 메모리를 insert
  // 응답을 클라이언트에게 보내기

  // 서버로 전송 
  end_serverfd = connect_endserver(hostname,port);

  if (end_serverfd < 0) {
    printf("connect failed\n");
    return;
  }
  sprintf(server_buf,"%s%s%s\r\n",method,path,version);
  printf("server : %s\n",server_buf);

  // 서버에 요청 메시지를 보냄 
  Rio_readinitb(&server_rio,end_serverfd);
  // 헤더 요청 읽을 읽으면서 - 서버에 보낼 요청 헤더 생성
  read_request_header(server_buf, hostname, path, port, &rio); // 클라이언트로부터 받은 요청을 수정하여 전송
  Rio_writen(end_serverfd,server_buf,strlen(server_buf)); // 데이터를 쓸 식별자, 데이터가 저장된 버퍼 주소, 버퍼에서 쓰는 데이터의 바이트 수

  // 서버에서 응답이 오면 클라에게 전달
  size_t n;
  while ((n=Rio_readlineb(&server_rio,server_buf,MAXLINE)) != 0)
  {
    strcat(response_buf,server_buf);
    Rio_writen(connfd,server_buf,n);
  }

  insert_cache(p,client_path,response_buf,MAX_OBJECT_SIZE);
  Close(end_serverfd);
  
}

// 목적지 서버에 보낼 HTTP 요청 헤
void read_request_header(char *http_header, char *hostname, char *path, int port, rio_t *server_rio) {
  char buf[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  sprintf(http_header,requestlint_hdr_format,path); 
  while (Rio_readlineb(server_rio,buf,MAXLINE) > 0) // 읽어온 데이터가 존재하는 동안
  {
    if (strcmp(buf,"\r\n") == 0) // 끝까지 다 읽었다면 break
      break; 

    // host header 값 만들기 : 항상 Host 헤더를 보냄
    if (!strncasecmp(buf,host_key,strlen(host_key))) { // buf에서 host_key와 일치하는 문자열을 찾아내면 그 문자열을 host_hdr에 복사
      strcpy(host_hdr,buf); // host_hdr에 buf를 복사
      continue;
    }

    // host 외 헤더 만들기 : 연결(connection), 프록시 연결(proxy-connection), 사용자 에이전트(user-agent) 헤더를 저장
    if(!strncasecmp(buf, connection_key, strlen(connection_key))
              &&strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
              &&strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
      {
          strcat(other_hdr, buf);
      }
  }

  // 필수 헤더가 없다면 추가로 전송 (hostname으로 만들기)
  // 클라이언트가 호스트 헤더를 제공하지 않은 경우), 서버에 대한 호스트 헤더를 생성하여 추가
  if (strlen(host_hdr) == 0 ) 
    sprintf(host_hdr,host_hdr_format,hostname);

  sprintf(http_header, "%s%s%s%s%s%s%s", http_header, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, "\r\n");
  return;
}

int connect_endserver(char *hostname, int port){
    char portStr[100];
    // portstr에 port 넣어주기
    sprintf(portStr, "%d", port);
    // 해당 hostname과 portStr로 end_server에게 가는 요청만들어주기
    return Open_clientfd(hostname, portStr);
}


// uri를 분석해서 파싱하기
void parse_uri(char *uri,char *hostname,char *path, int *port)
{
    *port = 80; // HTTP 기본 포트 80 (포트가 url에 포함 여부와 관계없이 작동해야함)

    char* pos = strstr(uri, "//"); // "//" 의 시작 위치에 대한 포인터를 리턴함
    pos = pos != NULL ? pos+2 : uri; 
    char *pos2 = strstr(pos, ":"); 

    if(pos2 != NULL) // 포트 번호가 있는 경우 
    {
        *pos2 = '\0'; // ':' 위치에서 분리 
        sscanf(pos, "%s", hostname); // pos 문자열에서 공백이 나타기 전까지 읽어서 hostname 변수에 저장
        sscanf(pos2+1, "%d%s", port, path); // portnum+1에서 시작하는 문자열에서 정수형(%d) 데이터와 문자열(%s) 데이터를 순서대로 읽어서 각각 port와 path 변수에 저장
    }
    else // 포트 번호가 없는 경우
    {
        pos2 = strstr(pos,"/");
        if(pos2!=NULL) // 경로가 있는 경우
        {
            *pos2 = '\0'; // '\' 위치에서 분리
            sscanf(pos,"%s",hostname);
            *pos2 = '/';
            sscanf(pos2,"%s",path);
        }
        else // 경로가 없는 경우
        {
            sscanf(pos,"%s",hostname);
        }
    }
    printf("---parse_uri host: %s, port: %ls, path: %s\n", hostname, port, path);
    return;
}
