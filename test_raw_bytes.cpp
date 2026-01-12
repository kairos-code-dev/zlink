#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

void print_hex(const char* label, const unsigned char* data, int len) {
    std::cout << label << " (" << len << " bytes): ";
    for (int i = 0; i < len; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::dec << std::endl;
}

int main() {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5); // ZMQ_DEALER

    zmq_bind(stream, "tcp://127.0.0.1:15562");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    zmq_connect(dealer, "tcp://127.0.0.1:15562");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Get routing ID
    char id_buf[256];
    char empty_buf[256];
    int id_len = zmq_recv(stream, id_buf, 256, 0);
    int empty_len = zmq_recv(stream, empty_buf, 256, 0);
    print_hex("Routing ID", (unsigned char*)id_buf, id_len);
    print_hex("Empty frame", (unsigned char*)empty_buf, empty_len);

    // Send test message
    unsigned char msg[] = {0x41, 0x42, 0x43, 0x44};  // "ABCD"
    std::cout << "\nSending 4 bytes..." << std::endl;
    int rc = zmq_send(dealer, msg, 4, 0);
    std::cout << "Send result: " << rc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Receive
    char recv_id[256];
    char recv_data[256];
    int rid_len = zmq_recv(stream, recv_id, 256, 0);
    int data_len = zmq_recv(stream, recv_data, 256, 0);

    print_hex("Received ID", (unsigned char*)recv_id, rid_len);
    print_hex("Received data", (unsigned char*)recv_data, data_len);

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
