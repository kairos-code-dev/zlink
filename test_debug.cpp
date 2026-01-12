#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

int main() {
    std::cout << "Creating context and sockets..." << std::endl;
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    std::cout << "Binding..." << std::endl;
    zmq_bind(stream, "tcp://127.0.0.1:15564");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Connecting..." << std::endl;
    zmq_connect(dealer, "tcp://127.0.0.1:15564");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Getting routing ID..." << std::endl;
    std::vector<char> id_buf(256);
    int id_len = zmq_recv(stream, id_buf.data(), 256, 0);
    std::cout << "ID: " << id_len << " bytes" << std::endl;

    std::cout << "Consuming initial data..." << std::endl;
    while (true) {
        int rc = zmq_recv(stream, id_buf.data(), 256, 1); // ZMQ_DONTWAIT
        if (rc == -1) {
            std::cout << "No more initial data (errno=" << zmq_errno() << ")" << std::endl;
            break;
        }
        std::cout << "Consumed " << rc << " bytes" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\nSending one test message..." << std::endl;
    char msg[4] = {'T', 'E', 'S', 'T'};
    int rc = zmq_send(dealer, msg, 4, 0);
    std::cout << "Send result: " << rc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::cout << "Receiving (non-blocking)..." << std::endl;
    rc = zmq_recv(stream, id_buf.data(), 256, 1); // ZMQ_DONTWAIT
    std::cout << "Recv ID: " << rc << " (errno=" << zmq_errno() << ")" << std::endl;

    if (rc > 0) {
        rc = zmq_recv(stream, id_buf.data(), 256, 0);
        std::cout << "Recv data: " << rc << " bytes" << std::endl;
    }

    std::cout << "Done!" << std::endl;
    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
