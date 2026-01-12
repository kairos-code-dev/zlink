#include <zmq.h>
#include <iostream>

int main() {
    void *ctx = zmq_ctx_new();

    std::cout << "Testing STREAM socket support..." << std::endl;
    void *stream = zmq_socket(ctx, 11); // ZMQ_STREAM = 11

    if (stream == nullptr) {
        std::cout << "FAILED: zmq_socket returned NULL for type 11" << std::endl;
        std::cout << "Error: " << zmq_errno() << " - " << zmq_strerror(zmq_errno()) << std::endl;
        zmq_ctx_term(ctx);
        return 1;
    }

    std::cout << "SUCCESS: STREAM socket (type 11) created" << std::endl;

    zmq_close(stream);
    zmq_ctx_term(ctx);
    return 0;
}
