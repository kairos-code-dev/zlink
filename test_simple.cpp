#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Creating context..." << std::endl;
    void *ctx = zmq_ctx_new();

    std::cout << "Creating STREAM socket..." << std::endl;
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM

    std::cout << "Creating DEALER socket..." << std::endl;
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    std::cout << "Binding STREAM..." << std::endl;
    int rc = zmq_bind(stream, "tcp://127.0.0.1:15561");
    std::cout << "Bind result: " << rc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Connecting DEALER..." << std::endl;
    rc = zmq_connect(dealer, "tcp://127.0.0.1:15561");
    std::cout << "Connect result: " << rc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Receiving connection notification..." << std::endl;
    char id_buf[256];
    char empty_buf[256];

    int id_len = zmq_recv(stream, id_buf, 256, 0);
    std::cout << "ID len: " << id_len << std::endl;

    int empty_len = zmq_recv(stream, empty_buf, 256, 0);
    std::cout << "Empty len: " << empty_len << std::endl;

    std::cout << "Sending one message from DEALER..." << std::endl;
    char msg[] = "ABCD";
    rc = zmq_send(dealer, msg, 4, 0);
    std::cout << "Send result: " << rc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Receiving at STREAM..." << std::endl;
    char recv_id[256];
    char recv_data[256];

    int rid_len = zmq_recv(stream, recv_id, 256, 0);
    std::cout << "Recv ID len: " << rid_len << std::endl;

    int data_len = zmq_recv(stream, recv_data, 256, 0);
    std::cout << "Recv data len: " << data_len << std::endl;

    if (data_len == 4) {
        std::cout << "SUCCESS! Got 4 bytes as expected" << std::endl;
    } else {
        std::cout << "FAIL! Got " << data_len << " bytes instead of 4" << std::endl;
    }

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
