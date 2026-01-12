#include <zmq.h>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // ZMQ STREAM socket
    void *ctx = zmq_ctx_new();
    void *stream_sock = zmq_socket(ctx, 11); // ZMQ_STREAM

    std::cout << "Binding STREAM socket..." << std::endl;
    zmq_bind(stream_sock, "tcp://127.0.0.1:5560");

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Raw TCP socket
    std::cout << "Creating raw TCP socket..." << std::endl;
    socket_t tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock == INVALID_SOCKET) {
        std::cout << "Failed to create socket" << std::endl;
        zmq_close(stream_sock);
        zmq_ctx_term(ctx);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5560);
#ifdef _WIN32
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
#else
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
#endif

    std::cout << "Connecting..." << std::endl;
    if (connect(tcp_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "Failed to connect" << std::endl;
        CLOSE_SOCKET(tcp_sock);
        zmq_close(stream_sock);
        zmq_ctx_term(ctx);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Receive connection notification
    std::cout << "Receiving connection notification..." << std::endl;
    char id_buf[256];
    char empty_buf[256];

    int id_len = zmq_recv(stream_sock, id_buf, 256, 0);
    std::cout << "Received routing ID: " << id_len << " bytes" << std::endl;

    int empty_len = zmq_recv(stream_sock, empty_buf, 256, 0);
    std::cout << "Received empty frame: " << empty_len << " bytes" << std::endl;

    // Send data from TCP
    std::cout << "Sending data from TCP socket..." << std::endl;
    char data[] = "Hello";
    send(tcp_sock, data, 5, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Receive at STREAM
    std::cout << "Receiving at STREAM socket..." << std::endl;
    char recv_id[256];
    char recv_data[256];

    int rid_len = zmq_recv(stream_sock, recv_id, 256, 0);
    std::cout << "Received routing ID: " << rid_len << " bytes" << std::endl;

    int data_len = zmq_recv(stream_sock, recv_data, 256, 0);
    std::cout << "Received data: " << data_len << " bytes: ";
    std::cout.write(recv_data, data_len) << std::endl;

    std::cout << "SUCCESS!" << std::endl;

    CLOSE_SOCKET(tcp_sock);
    zmq_close(stream_sock);
    zmq_ctx_term(ctx);
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
