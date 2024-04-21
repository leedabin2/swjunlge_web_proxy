/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

// 두 숫자를 더 하는 웹서비스
int main(void) 
{
  char *buf, *p, *method; // 쿼리스트링을 저장하기 위한 포인터, 문자열 내 특정위치를 가리키는 포인터, HTTP 메소드를 저장하기 위한 포인터
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE]; // 쿼리스트링에서 파싱한 두 인자 저장, content : HTTP 응답 본문을 구성하기 위한 문자열
  int n1=0, n2=0;

  method = getenv("REQUEST_METHOD");
  
  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&'); // 쿼리스트링을 읽어옴 &로 구분받아서 문자의 위치를 찾음
    *p = '\0'; // 널문자로 변경 후 첫 번째 인자를 종료 (buf 문자열을 두 부분으로 나눔)
    strcpy(arg1, buf); // 첫 번째 인자를 arg1에 복사
    strcpy(arg2, p+1); // 두 번째 인자의 시작점을 arg2에 복사
    n1 = atoi(arg1); // 정수로 변환
    n2 = atoi(arg2);
  }
  /* Make the response body */
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);
  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");

  // method가 GET일 경우에만 response body 보냄
  if(strcasecmp(method, "GET") == 0) { 
    printf("%s", content);
  }
  // printf("%s",content);
  fflush(stdout);

  exit(0);
}