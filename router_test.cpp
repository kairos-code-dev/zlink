#include <zmq.h>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <cassert>

void log(const char* msg) {
    std::cout << "[TEST] " << msg << std::endl;
}

int main() {
    void *ctx = zmq_ctx_new();
    void *r1 = zmq_socket(ctx, ZMQ_ROUTER);
    void *r2 = zmq_socket(ctx, ZMQ_ROUTER);

    // Explicitly set identities
    zmq_setsockopt(r1, ZMQ_ROUTING_ID, "ROUTER1", 7);
    zmq_setsockopt(r2, ZMQ_ROUTING_ID, "ROUTER2", 7);

    // Bind R1
    int rc = zmq_bind(r1, "inproc://router_test");
    assert(rc == 0);
    log("R1 bound to inproc://router_test");

    // Connect R2
    rc = zmq_connect(r2, "inproc://router_test");
    assert(rc == 0);
    log("R2 connected to inproc://router_test");

    // Give time for connection (inproc connects immediately but good practice)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // --- TEST 1: R2 sends to R1 ---
    log("Sending from R2 -> R1...");
    // Frame 1: Dest ID ("ROUTER1")
    zmq_send(r2, "ROUTER1", 7, ZMQ_SNDMORE);
    // Frame 2: Data
    zmq_send(r2, "Hello", 5, 0);
    log("Sent.");

    char buf[256];
    log("R1 receiving...");
    // Frame 1: Source ID ("ROUTER2")
    int len = zmq_recv(r1, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R1 received frame 1 (ID): ") + buf).c_str());
    
    // Frame 2: Data ("Hello")
    len = zmq_recv(r1, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R1 received frame 2 (Data): ") + buf).c_str());

    if (strcmp(buf, "Hello") == 0) log("TEST 1 SUCCESS");
    else log("TEST 1 FAILED");

    // --- TEST 2: R1 replies to R2 ---
    log("Sending reply from R1 -> R2...");
    // Frame 1: Dest ID ("ROUTER2")
    zmq_send(r1, "ROUTER2", 7, ZMQ_SNDMORE);
    // Frame 2: Data
    zmq_send(r1, "World", 5, 0);
    log("Sent.");

    log("R2 receiving...");
    len = zmq_recv(r2, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R2 received frame 1 (ID): ") + buf).c_str());

    len = zmq_recv(r2, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R2 received frame 2 (Data): ") + buf).c_str());

    if (strcmp(buf, "World") == 0) log("TEST 2 SUCCESS");
    else log("TEST 2 FAILED");

    zmq_close(r1);
    zmq_close(r2);
    zmq_ctx_term(ctx);
    return 0;
}
