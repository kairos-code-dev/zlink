#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

int main() {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    zmq_bind(stream, "tcp://127.0.0.1:15565");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    zmq_connect(dealer, "tcp://127.0.0.1:15565");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::vector<char> id_buf(256);
    int id_len = zmq_recv(stream, id_buf.data(), 256, 0);
    std::cout << "ID: " << id_len << " bytes" << std::endl;

    // Consume all initial data
    while (true) {
        int rc = zmq_recv(stream, id_buf.data(), 256, 1); // ZMQ_DONTWAIT
        if (rc == -1) break;
    }

    std::thread receiver([&]() {
        std::cout << "[Receiver] Waiting for message..." << std::endl;
        int rc1 = zmq_recv(stream, id_buf.data(), 256, 0);  // Blocking
        std::cout << "[Receiver] Got routing ID: " << rc1 << std::endl;

        int rc2 = zmq_recv(stream, id_buf.data(), 256, 0);  // Blocking
        std::cout << "[Receiver] Got data: " << rc2 << " bytes" << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "[Sender] Sending message..." << std::endl;
    char msg[4] = {'T', 'E', 'S', 'T'};
    int rc = zmq_send(dealer, msg, 4, 0);
    std::cout << "[Sender] Send result: " << rc << std::endl;

    receiver.join();
    std::cout << "SUCCESS!" << std::endl;

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
