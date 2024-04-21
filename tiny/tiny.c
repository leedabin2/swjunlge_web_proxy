/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// port 번호를 인자로 받아서 클라이언트에 요청이 들어 올때마다 새로운 연결 소켓을 만들어서 doit 함수 호출
int main(int argc, char **argv) {
  int listenfd, connfd; // 듣기 식별자, 연결 식별자
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; // 클라이언트 연결 요청 후에 클라이언트 연결 소켓주소

  if (argc != 2) { // 명령줄 인수의 수가 맞지 않으면
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // 프로그램이름과, 포트 번호를 지정하라는 메시
    exit(1); // 종료 코드
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
    doit(connfd);   // line:netp:tiny:doi 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close 자신과의 연결 끊음(서버 연결 식별자 닫음)
  }
}

// 한 개의 HTTP 트랜잭션을 처리한다.(서버가 클라로부터 하나의 http요청과 응답을 처리한다)
void doit(int fd) {
  // 변수 선언 및 초기화 
  int is_static; // 정적인지 아닌지 판단하는 변수
  struct stat sbuf; // 파일에 대한 정보를 가지는 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 클라이언트로부터 받은 요청라인을 저장하는 문자열 배열
  char filename[MAXLINE], cgiargs[MAXLINE]; // 요청된 uri를 분석한 결과를 저장하는 문자열 배열(요청된 파일 이름, CGI실행 시 필요한 인자)
  rio_t rio; // rio 패키지를 사용하기 위한 구조체, 네트워크 통신을 위해 사용

  // 클라이언트 요청읽기 : rio로 보낸 요청라인, 헤더를 읽고 분석
  Rio_readinitb(&rio,fd); // rio 버퍼를 초기화하고 파일 디스크립터(fd)와 연결 (한개의 빈 버퍼를 설정하고, 이 버퍼와 한 개의 오픈한 파일 식별자를 연결)
  Rio_readlineb(&rio,buf,MAXLINE); // 클라이언트로부터 요청 라인을 읽어서 buf에 저장
  printf("Request headers: \n"); 
  printf("%s",buf); // GET: gozilla.gif
  sscanf(buf,"%s %s %s", method, uri, version); // 요청 라인에서 메소드, uri, 버전을 분리하여 저장

  // HTTP 메소드 검사 : GET 방식이나 HEAD 방식이 아니면, 에러로 연결
  if (!(strcasecmp(method,"GET") == 0 || strcasecmp(method, "HEAD") ==0)) {
    clienterror(fd,method,"501","Not implemented", "Tiny does not implement this method");
    return;
  }

  // 요청 헤더 읽기
  read_requesthdrs(&rio); // GET 메서드를 읽어들임

  // uri 분석, 요청이 정적인지 동적 내용 요청인지를 결정하여 변수에 저장
  is_static = parse_uri(uri,filename,cgiargs);

  // 요청된 파일의 정보 읽기
  if(stat(filename, &sbuf) < 0) { // filename 유효성 검사
    clienterror(fd,filename,"404","Not found","Tiny couldn't find this file");
    return;
  }

  if(is_static) { // st_mode는 파일권한 비트와 파일 타입 모두를 인코딩하고 있음
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) // 일반파일인지, 읽기 권한이 있는지  
    {
      clienterror(fd,filename,"403","Forbidden","Tiny couldn't read the file");
      return;
    }
    serve_static(fd,filename,sbuf.st_size);// 정적 컨텐츠를 클라이언트에게 제공
  } else {
    // 동적이라면 실행가능한지 검증
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR && sbuf.st_mode)) {
      clienterror(fd,filename,"403","Forbidden","Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd,filename,cgiargs); // CGI 프로그램 실행 한 뒤 결과를 클라이언트에게 전송
  }
}


// HTTP 응답을 응답 라인에 적절한 상태코드와 상태메시지와 함께 클라이언트에 보냄
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // HTML 형식 response body
  sprintf(body,"<html><title>Tiny Error</title>");
  sprintf(body,"%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body,"%s%s: %s\r\n",body,errnum,shortmsg);
  sprintf(body,"%s<p>%s: %s\r\n",body,longmsg,cause);
  sprintf(body,"%s<hr><em>The Tiny Web server</em>\r\n",body);

  // response 쓰기
  sprintf(buf,"HTTP/1.0 %s %s\r\n",errnum,shortmsg);
  Rio_writen(fd,buf,strlen(buf));
  sprintf(buf,"Content-type: text/html\r\n");
  Rio_writen(fd,buf,strlen(buf));
  sprintf(buf,"Content-length: %d\r\n\r\n",(int)strlen(body));

  // 에러 메시지와 응답을 서버 소켓을 통해 클라이언트에게 보
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));  
}

// HTTP 요청의 헤더를 읽고, 출력하는 기능을 수행 (클라이언트로 부터 받은 요청의 헤더를 읽지만, 실질적으로 처리하지 않음)
void read_requesthdrs(rio_t *rp) { // 네트워크 통신에서 버퍼링된 입출력을 다루기 위함
  char buf[MAXLINE]; // 요청 헤더의 각 라인을 저장하기 위한 문자 배열

  Rio_readlineb(rp,buf,MAXLINE); // rp 가 가리키는 입력 스트림에서 한 줄을 읽어 buf에 저장
  while (strcmp(buf,"\r\n")) // 헤더의 끝을 만날 때까지 반복 읽기 (빈줄을 만나면 헤더 읽기 멈춤)
  {
    Rio_readlineb(rp,buf,MAXLINE); // 추가적인 헤더라인 읽기
    printf("%s",buf);
  }
  return;
}

// uri를 받아 요청받은 filename, cgiargs를 반
int parse_uri(char *uri, char *filename, char *cgiargs ) {
  char *ptr;

  if (!strstr(uri,"cgi-bin")) { // 정적 컨텐츠
    strcpy(cgiargs,"");
    strcpy(filename,".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename,"home.html");

    return 1; 
  }
  else { // 동적 컨텐츠(cgi-bin은 동적 파일로 분류(과제요구사항))
    ptr = index(uri,"?");
    if (ptr) { // ?가 있으면
      strcpy(cgiargs,ptr+1); 
      *ptr = '\0';
    }
    else {
      strcpy(cgiargs,"");
    }
    strcpy(filename,".");
    strcat(filename,uri);
    return 0;
  } 
}

// 클라이언트가 원하는 정적 컨텐츠를 받아옴
// 응답 라인과 헤더를 작성 후 서버에 보내고, 정적 컨텐츠 파일을 읽어 응답을 클라에 보냄
void serve_static(int fd, char *filename, int filesize) {
  int srcfd;

  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename,filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd,buf,strlen(buf));
  printf("Response headers: \n");
  printf("%s", buf);

  // 클라에게 response body로 보내줌
  srcfd = Open(filename, O_RDONLY, 0);

  // mmap 함수는 요청한 파일을 가상메모리 영역으로 매핑
  srcp = Mmap(0,filesize,PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd,srcp,filesize); // 주소 srcp에서 시작하는 filesize 바이트를 클라의 연결 식별자로 복
  Munmap(srcp,filesize); 
}

// filename을 조사해서 각각 식별자에 맞는 MIME타입을 filetype에 입
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename,".html"))
    strcpy(filetype,"text/html");
  else if (strstr(filename,".gif"))
    strcpy(filetype,"image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype,"image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype,"image/jpeg");
  else 
    strcpy(filetype,"text/plain");
}

// 동적 디렉토리를 받을 때, 응답라인과 헤더를 작성하고 서버에 보냄 
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf)); // 지정된 바이트 수(n)을 fd로 성공적으로 전송하거나 오류가 발생할 때까지 반복적으로 시도 (실제로 쓰인 바이트 수를 반환)
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 서버가 GET /cgi-bin/adder HTTP/1.1 와 같은 요청을 받으면 하는 일
  if (Fork() == 0) { // 자식 프로세스를 생성
  /* Real server would set all CGI vars here */
  setenv("QUERY_STRING", cgiargs, 1);// 자식프로세스 : execve를 호출하기 전 자식프로세스는 cgi 환경변수 쿼리스트링을 설정하고 
  // method를 cgi-bin/adder.c에 넘겨주기 위해 환경변수 set(인자를 서버에 넘겨 준다?)
  //setenv("REQUEST_METHOD", method, 1);
  Dup2(fd, STDOUT_FILENO); // CGI 프로세스 출력을 fd로 복사(표준 출력을 fd(클아이언트와 연계된 연결 식별자로 재지정), 따라서 표준 출력으로 쓰는 모든 것은 클라이언트로 직접 감
  Execve(filename, emptylist, environ); // 파일 이름이 첫번째 인자인 것과 같은 파일을 실행
  // /cgi-bin/adder 프로그램을 자식의 컨텍스트에서 실행
  } 
  Wait(NULL); // 자식프로세스가 완전히 실행을 마치고 종료될 때까지 부모 프로세스의 실행을 일시중
 
}