#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUF_SIZE 1024
#define MAX_CLIENTS 4
#define NICKNAME_SIZE 20

//각 클라이언트에 대한 정보를 저장할 수 있는 구조체
struct client_data {
    int fd;                        // 클라이언트 소켓 파일 디스크립터
    pthread_t thread;              // 클라이언트 통신을 처리하는 스레드
    char nickname[NICKNAME_SIZE];  // 클라이언트 닉네임
};

struct client_data *clients[MAX_CLIENTS]; //연결된 각 클라이언트의 client_data 구조체 포인터를 저장 할 배열
int client_count = 0;                     //현재 연결된 클라이언트 수를 추적
pthread_mutex_t clients_lock;             //공유 자원(clients 배열과 client_count)을 보호하기 위한 뮤텍스

void *handle_client(void *arg);
void send_message_to_all(char *message, int len, int sender_fd);
void notify_client_status(char *message);

int main(int argc, char *argv[]) {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;
    // argv[0]="./server";
    // argv[1]="<your-ip>";
    // argv[2]="<port-number>";
    // argc=3;
    if (argc != 3) {
        printf("Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    // 서버 소켓 생성 및 바인딩
    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (server_sock == -1)
        perror("socket() error");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
        perror("bind() error");

    if (listen(server_sock, MAX_CLIENTS) == -1)
        perror("listen() error");

    printf("Server is now open and listening on %s:%s\n", argv[1], argv[2]);
    
    pthread_mutex_init(&clients_lock, NULL);

    while (1) {
        client_addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_size);  // 클라이언트 수락
        if (client_sock == -1) {
            perror("accept() error");
            continue;
        }

        pthread_mutex_lock(&clients_lock);
        if (client_count >= MAX_CLIENTS) {
            pthread_mutex_unlock(&clients_lock);
            close(client_sock);
            continue;
        }

        struct client_data *client = (struct client_data*)malloc(sizeof(struct client_data));
        client->fd = client_sock;
        clients[client_count++] = client;

        pthread_create(&client->thread, NULL, handle_client, (void*)client);  //각 클라이언트를 처리하기 위해 새로운 스레드를 시작
        pthread_mutex_unlock(&clients_lock);

        printf("Connected client: %d\n", client_sock);
    }

    close(server_sock);
    return 0;
}

//클라이언트 메시지 처리 함수
void *handle_client(void *arg) { 
    struct client_data *client = (struct client_data*)arg;
    char buf[BUF_SIZE];
    int str_len;

    // client로부터 전송된 닉네임을 읽음
    str_len = read(client->fd, client->nickname, NICKNAME_SIZE - 1);
    if (str_len <= 0) {
        close(client->fd);
        free(client);
        return NULL;
    }
    client->nickname[str_len] = '\0';

    // 새로운 클라이언트가 접속했음을 나머지 클라이언트들에게 알림
    char notify_message[BUF_SIZE];
    snprintf(notify_message, BUF_SIZE, "[%s has joined. Current users: %d]\n", client->nickname, client_count);
    notify_client_status(notify_message);
    //반복문 내부에서 뮤텍스를 이용해 임계구역 보호 --> clients 배열과 client_count 같은 공유 자원에 안전하게 접근할 수 있도록 함.
    while (1) {
        //메세지를 읽음
        memset(buf, 0, BUF_SIZE);
        str_len = read(client->fd, buf, BUF_SIZE - 1);
        // 클라이언트 연결이 종료되었을 때 적절하게 클라이언트를 정리하고 다른 클라이언트에게 알림
        if (str_len <= 0) {
            close(client->fd);
            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < client_count; i++) {
                if (clients[i]->fd == client->fd) {
                    clients[i] = clients[client_count - 1];
                    client_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            snprintf(notify_message, BUF_SIZE, "[%s has left. Current users: %d]\n", client->nickname, client_count);
            notify_client_status(notify_message);

            free(client);
            printf("Closed client: %d\n", client->fd);
            return NULL;
        }

        buf[str_len-1] = '\0';

        // quit메세지인지 확인 후 해당 클라이언트 종료
        if (strcmp(buf, "quit\n") == 0 || strcmp(buf, "quit") == 0) {
            close(client->fd);
            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < client_count; i++) {
                if (clients[i]->fd == client->fd) {
                    clients[i] = clients[client_count - 1];
                    client_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            snprintf(notify_message, BUF_SIZE, "[%s has left. Current users: %d]\n", client->nickname, client_count);
            notify_client_status(notify_message);

            free(client);
            printf("Closed client: %d\n", client->fd);
            return NULL;
        }

        char message[BUF_SIZE + NICKNAME_SIZE];
        snprintf(message, sizeof(message), "%s: %s", client->nickname, buf);
        send_message_to_all(message, strlen(message), client->fd);
    }
}
//보낸 클라이언트를 제외한 모든 클라이언트에게 메시지를 전송하는 함수
void send_message_to_all(char *message, int len, int sender_fd) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i]->fd != sender_fd) {
            write(clients[i]->fd, message, len);
        }
    }
    pthread_mutex_unlock(&clients_lock);
}
//상태 메시지(접속/퇴장 알림)를 모든 클라이언트에게 알리는 함수
void notify_client_status(char *message) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < client_count; i++) {
        write(clients[i]->fd, message, strlen(message));
    }
    pthread_mutex_unlock(&clients_lock);
}
