#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

int main() {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    zmq_bind(stream, "tcp://127.0.0.1:15563");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    zmq_connect(dealer, "tcp://127.0.0.1:15563");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Get routing ID and consume handshake
    std::vector<char> id_buf(256);
    std::vector<char> recv_buf(256);

    int id_len = zmq_recv(stream, id_buf.data(), 256, 0);
    std::vector<char> routing_id(id_buf.begin(), id_buf.begin() + id_len);
    std::cout << "Routing ID: " << id_len << " bytes" << std::endl;

    // Consume empty frame (or handshake data)
    while (true) {
        int rc = zmq_recv(stream, recv_buf.data(), 256, 1); // ZMQ_DONTWAIT
        if (rc == -1) break;  // No more data
        std::cout << "Consumed " << rc << " bytes" << std::endl;
    }

    std::cout << "\nStarting message test..." << std::endl;

    // Test: send 100 messages
    int msg_count = 100;
    int sent = 0, recv_count = 0;
    std::vector<char> buffer(4, 'X');

    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; i++) {
            int rc1 = zmq_recv(stream, id_buf.data(), 256, 0);
            int rc2 = zmq_recv(stream, recv_buf.data(), 256, 0);

            if (rc1 < 0 || rc2 < 0) {
                std::cout << "Recv error at " << i << ": rc1=" << rc1 << " rc2=" << rc2 << std::endl;
                break;
            }

            recv_count++;

            if (rc2 != 4) {
                std::cout << "Wrong size at " << i << ": expected 4, got " << rc2 << std::endl;
                break;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (int i = 0; i < msg_count; i++) {
        int rc = zmq_send(dealer, buffer.data(), 4, 0);
        if (rc == 4) {
            sent++;
        } else {
            std::cout << "Send error at " << i << std::endl;
            break;
        }
    }

    receiver.join();

    std::cout << "sent=" << sent << " recv=" << recv_count << std::endl;
    if (sent == msg_count && recv_count == msg_count) {
        std::cout << "SUCCESS!" << std::endl;
    } else {
        std::cout << "FAILED!" << std::endl;
    }

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
