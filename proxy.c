#include <stdio.h>
#include "csapp.h"

// 기본 시퀀셜 프록시 HTTP/1.0 GET 요청을 처리
// http://www.cmu.edu/hub/index.html HTTP/1.1 으로 들어오면
// 프록시 : GET /hub/index.html HTTP/1.0 으로 서버에게 요청 보냄

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

// 클라이언트로 요청이 들어오면 새로운 연결을 생성 후 원격 서버에 해당 요청을 전달
int main(int argc, char **argv) {

  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];  // 프록시가 요청을 받고 응답해줄 클라이언트의 IP, Port
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; 

  if (argc != 2) {  // 명령줄 인수의 수가 맞지 않으면
    fprintf(stderr, "usage: %s <port>\n", argv[0]);  // 프로그램이름과, 포트 번호를 지정하라는 메시
    exit(1); 
  }

  listenfd = Open_listenfd(argv[1]); // 듣기 소켓을 오픈 (해당 포트 번호에 해당하는)

  while (1) { // 클라이언트한테 받은 연결 요청을 accept (무한 루프)
    clientlen = sizeof(clientaddr);
    // clientfd, connfd 사이의 연결을 수립
    connfd = Accept(listenfd, (SA *)&clientaddr, // 듣기식별자에 도달하기를 기다리고, 클라이언트 소켓 주소를 채우고, 연결 식별자를 리턴
                    &clientlen);  // line:netp:tiny:accept 반복적으로 연결 요청을 접수
    
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0); // hostname, port 반환 -> 서버가 클라이언트의 주소 정보를 알아내는 용도
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd); // 프록시가 중계를 시작
    Close(connfd); 
  }
  return 0;
}

// 클라이언트의 연결요청이 유효하면, 웹서버에 연결 설정
void doit(int connfd) {

  int end_serverfd; // 연결할 웹서버용 fd 생성
  int port;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 클라이언트로부터 받은 요청라인을 저장
  char hostname[MAXLINE],path[MAXLINE];
  char endserver_http_header[MAXLINE]; // 서버에 보낼 요청 헤더 생성

  rio_t rio, server_rio; // 클라용 rio, 서버용 rio (네트워크 통신을 위함)

  // 클라이언트 요청 읽기(클라와의 fd를 클라용 rio에 연결)
  Rio_readinitb(&rio,connfd); 
  Rio_readlineb(&rio,buf,MAXLINE);
  printf("Request headers: \n"); 
  printf("%s",buf);
  sscanf(buf,"%s %s %s", method, uri, version);  // 요청라인에서 메소드, uri, version 분리해서 저장

  // HTTP 요청 검사 - GET이 아니면 에러 처리
  if (!(strcasecmp(method,"GET") == 0)) {
    printf("Server provide only GET method");
    // 추후 클라이언트 에러함수로 수정
    return;
  }

  // uri를 분석해서 파싱하기(uri,호스트네임,목적지호스트, 포트)
  parse_uri(uri,hostname,path, &port); 

  // 헤더 요청 읽을 읽으면서 - 서버에 보낼 요청 헤더 생성
  read_request_header(endserver_http_header, hostname, path, port, &rio);

  // 서버로 전송
  end_serverfd = connect_endserver(hostname,port);
  if (end_serverfd < 0) {
    printf("connect failed\n");
    return;
  }

  // 서버에 요청 메시지를 보냄 
  Rio_readinitb(&server_rio,end_serverfd);
  Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header)); // 데이터를 쓸 식별자, 데이터가 저장된 버퍼 주소, 버퍼에서 쓰는 데이터의 바이트 수

  // 서버에서 응답이 오면 클라에게 전달
  size_t n;
  while ((n=Rio_readlineb(&server_rio,buf,MAXLINE)) != 0)
  {
    Rio_writen(connfd,buf,n);
  }
  Close(end_serverfd);
  
}

// HTTP 요청 헤더를 읽음
void read_request_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  sprintf(request_hdr,requestlint_hdr_format,path); // request_hdr에 reqquestlint_hdr_format을 담음(path인자는 reqquestlint_hdr_format에 들어갈 값)
  while (Rio_readlineb(client_rio,buf,MAXLINE) > 0) // 읽어온 데이터가 존재하는 동안
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
              &&!strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
              &&!strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
      {
          strcat(other_hdr, buf);
      }
  }
  // 필수 헤더가 없다면 추가로 전송 (hostname으로 만들기)
  // 클라이언트가 호스트 헤더를 제공하지 않은 경우), 서버에 대한 호스트 헤더를 생성하여 추가
  if (strlen(host_hdr) == 0 ) 
    sprintf(host_hdr,host_hdr_format,hostname);

  sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, "\r\n");
  return;
}

inline int connect_endserver(char *hostname, int port){
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
    return;
}


