#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5);  // ZMQ_DEALER

    zmq_bind(stream, "tcp://127.0.0.1:15555");
    zmq_connect(dealer, "tcp://127.0.0.1:15555");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Sending from DEALER..." << std::endl;
    char data[] = "test";
    zmq_send(dealer, data, 4, 0);

    std::cout << "Receiving at STREAM..." << std::endl;
    char buf[256];
    char routing_id_buf[256];

    // Connection notification
    int rc1 = zmq_recv(stream, routing_id_buf, 256, 0);
    std::cout << "Received notification frame 1 (routing_id): " << rc1 << " bytes" << std::endl;

    int rc2 = zmq_recv(stream, buf, 256, 0);
    std::cout << "Received notification frame 2 (empty): " << rc2 << " bytes" << std::endl;

    // Actual data
    int rc3 = zmq_recv(stream, buf, 256, 0);
    std::cout << "Received data frame 1 (routing_id): " << rc3 << " bytes" << std::endl;

    int rc4 = zmq_recv(stream, buf, 256, 0);
    std::cout << "Received data frame 2 (data): " << rc4 << " bytes: " << std::string(buf, rc4) << std::endl;

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);

    std::cout << "SUCCESS!" << std::endl;
    return 0;
}
