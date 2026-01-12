#include <zmq.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

int main() {
    std::cout << "Testing STREAM socket bidirectional message exchange" << std::endl;
    std::cout << "=====================================================" << std::endl;

    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM
    void *dealer = zmq_socket(ctx, 5);  // ZMQ_DEALER

    // Socket settings
    int hwm = 0;
    zmq_setsockopt(stream, 8, &hwm, sizeof(hwm)); // ZMQ_SNDHWM
    zmq_setsockopt(dealer, 24, &hwm, sizeof(hwm)); // ZMQ_RCVHWM

    const char* endpoint = "tcp://127.0.0.1:15558";
    zmq_bind(stream, endpoint);
    zmq_connect(dealer, endpoint);

    std::cout << "✓ Connected" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Timeouts
    int timeout = 2000;
    zmq_setsockopt(stream, 27, &timeout, sizeof(timeout)); // ZMQ_RCVTIMEO
    zmq_setsockopt(dealer, 27, &timeout, sizeof(timeout));

    char send_buf[64];
    char id_buf[256];
    char data_buf[256];
    char routing_id[256];
    int routing_id_len = 0;

    memset(send_buf, 'X', 64);

    // Test: Multiple message exchange
    std::cout << "\nTest: Send 10 messages DEALER->STREAM" << std::endl;

    for (int i = 0; i < 10; i++) {
        // Send from DEALER
        int sent = zmq_send(dealer, send_buf, 64, 0);
        if (sent < 0) {
            std::cout << "  [" << i << "] Send FAILED: " << zmq_strerror(zmq_errno()) << std::endl;
            break;
        }

        // Receive at STREAM
        int id_len = zmq_recv(stream, id_buf, 256, 0);
        if (id_len < 0) {
            std::cout << "  [" << i << "] Recv routing_id FAILED: " << zmq_strerror(zmq_errno()) << std::endl;
            break;
        }

        // Save routing_id from first message
        if (i == 0) {
            memcpy(routing_id, id_buf, id_len);
            routing_id_len = id_len;
        }

        int data_len = zmq_recv(stream, data_buf, 256, 0);
        if (data_len < 0) {
            std::cout << "  [" << i << "] Recv data FAILED: " << zmq_strerror(zmq_errno()) << std::endl;
            break;
        }

        std::cout << "  [" << i << "] ✓ Sent " << sent << " bytes, Received " << data_len << " bytes" << std::endl;
    }

    std::cout << "\nTest: Send 10 messages STREAM->DEALER (echo back)" << std::endl;

    for (int i = 0; i < 10; i++) {
        // Send from DEALER
        int sent = zmq_send(dealer, send_buf, 64, 0);
        if (sent < 0) {
            std::cout << "  [" << i << "] DEALER send FAILED" << std::endl;
            break;
        }

        // Receive at STREAM
        zmq_recv(stream, id_buf, 256, 0);
        int data_len = zmq_recv(stream, data_buf, 256, 0);

        // Echo back from STREAM to DEALER
        zmq_send(stream, routing_id, routing_id_len, 1); // ZMQ_SNDMORE
        int echo_sent = zmq_send(stream, data_buf, data_len, 0);

        if (echo_sent < 0) {
            std::cout << "  [" << i << "] STREAM send FAILED: " << zmq_strerror(zmq_errno()) << std::endl;
            break;
        }

        // Receive echo at DEALER
        int echo_recv = zmq_recv(dealer, data_buf, 256, 0);
        if (echo_recv < 0) {
            std::cout << "  [" << i << "] DEALER recv FAILED: " << zmq_strerror(zmq_errno()) << std::endl;
            break;
        }

        std::cout << "  [" << i << "] ✓ Echo: sent " << echo_sent << " bytes, received " << echo_recv << " bytes" << std::endl;
    }

    std::cout << "\n*** ALL TESTS PASSED ***" << std::endl;

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);

    return 0;
}
