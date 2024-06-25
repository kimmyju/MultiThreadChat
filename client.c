#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>    
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <string.h>

#define PORT <port-number>

int connected = 0;  // 클라이언트 연결 상태를 나타내는 변수
// 서버로부터 메시지를 수신하는 스레드 함수
void *my_thread(void *arg)
{
    int fd = *(int *)arg;  // 클라이언트 소켓 파일 디스크립터
    char s[256];  // 수신된 메시지를 저장할 버퍼
    int n;

    do {
        n = read(fd, s, 256);  // 서버로부터 메시지 읽기
        if (n > 0) {
            s[n] = '\0';  // 문자열 종료 처리
            printf("%s\n", s);  // 수신된 메시지 출력
        }
    } while (n > 0);

    connected = 0;  // 연결이 끊어진 경우 상태 변경
    close(fd);      // 소켓 닫기

    return NULL;
}

int main(int argc, char **argv)
{
    struct sockaddr_in ad;   // 서버 주소 정보를 저장할 구조체
    int fd;    // 클라이언트 소켓 파일 디스크립터
    pthread_t tid;  // 수신 스레드 ID
    int port = PORT;  // 기본 포트 번호

    if (argc >= 2) {
        sscanf(argv[1], "%d", &port);  // 명령줄 인자로 포트 번호 설정
        printf("port = %d\n", port);
    }
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  // 소켓 생성
        printf("socket error\n");
        return -1;
    }

    memset(&ad, 0, sizeof(ad));  // 서버 주소 구조체 초기화
    ad.sin_family = AF_INET;     // 주소 체계 설정 (IPv4)
    ad.sin_addr.s_addr = inet_addr(<your-ip>);  // 서버 IP 설정
    ad.sin_port = htons(port);  // 포트 번호 설정

    if (connect(fd, (struct sockaddr *)&ad, sizeof(ad)) != 0) {  // 서버에 연결
        printf("connection failed\n");
        return -1;
    }

    connected = 1;  // 연결 상태 설정
    pthread_create(&tid, NULL, my_thread, &fd);  // 수신 스레드 생성

    // 서버로 닉네임 전송
    char nickname[256];
    printf("Enter your nickname: ");
    fgets(nickname, 256, stdin);
    nickname[strcspn(nickname, "\n")] = '\0';
    write(fd, nickname, strlen(nickname));

    while (connected) {  // 연결된 동안 메시지 송신 루프
        char buf[256];
        scanf("%[^\n]", buf);  // 사용자로부터 메시지 입력
        scanf("%*c");
        write(fd, buf, strlen(buf) + 1);  // 서버로 메시지 전송
        if (strcmp(buf, "quit") == 0) {  // quit 명령어 처리
            sleep(1);
            close(fd);
            exit(0);
        }
    }
}