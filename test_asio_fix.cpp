#include <zmq.h>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <chrono>

#ifndef ZMQ_STREAM
#define ZMQ_STREAM 11
#endif

int main() {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, ZMQ_STREAM);
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    // Socket settings
    int hwm = 0;
    zmq_setsockopt(stream, 6, &hwm, sizeof(hwm)); // ZMQ_SNDHWM
    zmq_setsockopt(dealer, 24, &hwm, sizeof(hwm)); // ZMQ_RCVHWM

    // Bind and connect
    zmq_bind(stream, "tcp://127.0.0.1:15560");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    zmq_connect(dealer, "tcp://127.0.0.1:15560");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(4, 'X');
    std::vector<char> recv_buf(256);
    std::vector<char> id_buf(256);
    std::vector<char> routing_id;

    // Get routing ID
    int id_len = zmq_recv(stream, id_buf.data(), 256, 0);
    if (id_len > 0) {
        routing_id.assign(id_buf.begin(), id_buf.begin() + id_len);
        std::cout << "Got routing ID: " << id_len << " bytes" << std::endl;
    }
    int empty_len = zmq_recv(stream, recv_buf.data(), 256, 0);
    std::cout << "Got empty frame: " << empty_len << " bytes" << std::endl;

    // Test with increasing message counts
    int test_counts[] = {1000, 10000, 50000, 100000, 200000};

    for (int test_idx = 0; test_idx < 5; test_idx++) {
        int msg_count = test_counts[test_idx];
        std::cout << "\nTesting " << msg_count << " messages..." << std::endl;

        int sent = 0, recv_count = 0, valid_count = 0;

        // Receiver thread
        std::thread receiver([&]() {
            for (int i = 0; i < msg_count; i++) {
                int rc1 = zmq_recv(stream, id_buf.data(), 256, 0);
                int rc2 = zmq_recv(stream, recv_buf.data(), 256, 0);

                if (rc1 < 0 || rc2 < 0) {
                    std::cout << "Recv error at " << i << ": rc1=" << rc1
                              << " rc2=" << rc2 << " errno=" << errno << std::endl;
                    break;
                }

                recv_count++;
                if (rc2 == 4) {
                    valid_count++;
                }

                if (rc2 != 4) {
                    std::cout << "Wrong size at " << i << ": " << rc2 << " bytes" << std::endl;
                    break;
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Send messages
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < msg_count; i++) {
            int rc = zmq_send(dealer, buffer.data(), 4, 0);
            if (rc == 4) {
                sent++;
            } else {
                std::cout << "Send error at " << i << std::endl;
                break;
            }
        }
        auto send_end = std::chrono::steady_clock::now();

        receiver.join();
        auto end = std::chrono::steady_clock::now();

        auto send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - start).count();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        std::cout << "sent=" << sent << " recv=" << recv_count << " valid=" << valid_count << std::endl;
        std::cout << "send_time=" << send_ms << "ms total_time=" << total_ms << "ms" << std::endl;

        if (recv_count != msg_count || valid_count != msg_count) {
            std::cout << "FAILED at " << msg_count << " messages!" << std::endl;
            break;
        } else {
            std::cout << "SUCCESS!" << std::endl;
        }
    }

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
