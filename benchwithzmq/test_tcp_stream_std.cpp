#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>

int main() {
    std::cout << "Testing STREAM socket with TCP transport (standard libzmq)" << std::endl;
    std::cout << "============================================================" << std::endl;

    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5);  // ZMQ_DEALER

    if (!stream || !dealer) {
        std::cout << "FAILED: Socket creation failed" << std::endl;
        zmq_ctx_term(ctx);
        return 1;
    }

    std::cout << "✓ Sockets created" << std::endl;

    // Socket settings
    int hwm = 0;
    zmq_setsockopt(stream, 8, &hwm, sizeof(hwm)); // ZMQ_SNDHWM
    zmq_setsockopt(dealer, 24, &hwm, sizeof(hwm)); // ZMQ_RCVHWM

    // Bind STREAM, connect DEALER with TCP
    const char* endpoint = "tcp://127.0.0.1:15556";

    int rc1 = zmq_bind(stream, endpoint);
    if (rc1 != 0) {
        std::cout << "FAILED: zmq_bind returned " << rc1 << std::endl;
        std::cout << "Error: " << zmq_strerror(zmq_errno()) << std::endl;
        zmq_close(dealer);
        zmq_close(stream);
        zmq_ctx_term(ctx);
        return 1;
    }
    std::cout << "✓ STREAM bound to " << endpoint << std::endl;

    int rc2 = zmq_connect(dealer, endpoint);
    if (rc2 != 0) {
        std::cout << "FAILED: zmq_connect returned " << rc2 << std::endl;
        zmq_close(dealer);
        zmq_close(stream);
        zmq_ctx_term(ctx);
        return 1;
    }
    std::cout << "✓ DEALER connected to " << endpoint << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test 1: Send message from DEALER
    std::cout << "\nTest 1: DEALER -> STREAM" << std::endl;
    char send_buf[64];
    memset(send_buf, 'A', 64);

    std::cout << "  Sending 64 bytes from DEALER..." << std::endl;
    int sent = zmq_send(dealer, send_buf, 64, 0);
    if (sent < 0) {
        std::cout << "  FAILED: zmq_send returned " << sent << std::endl;
        std::cout << "  Error: " << zmq_strerror(zmq_errno()) << std::endl;
    } else {
        std::cout << "  ✓ Sent " << sent << " bytes" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try to receive with timeout
    std::cout << "  Receiving at STREAM (with 2s timeout)..." << std::endl;

    // Set receive timeout
    int timeout = 2000;
    zmq_setsockopt(stream, 27, &timeout, sizeof(timeout)); // ZMQ_RCVTIMEO

    char id_buf[256];
    char data_buf[256];

    int id_len = zmq_recv(stream, id_buf, 256, 0);
    if (id_len < 0) {
        std::cout << "  FAILED: First zmq_recv (routing_id) returned " << id_len << std::endl;
        std::cout << "  Error: " << zmq_strerror(zmq_errno()) << std::endl;
        std::cout << "\n*** TCP STREAM SOCKET APPEARS TO BE BROKEN ***" << std::endl;
    } else {
        std::cout << "  ✓ Received routing_id: " << id_len << " bytes" << std::endl;

        int data_len = zmq_recv(stream, data_buf, 256, 0);
        if (data_len < 0) {
            std::cout << "  FAILED: Second zmq_recv (data) returned " << data_len << std::endl;
            std::cout << "  Error: " << zmq_strerror(zmq_errno()) << std::endl;
        } else {
            std::cout << "  ✓ Received data: " << data_len << " bytes" << std::endl;
            std::cout << "\n*** SUCCESS: TCP STREAM SOCKET WORKS! ***" << std::endl;
        }
    }

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);

    return 0;
}
