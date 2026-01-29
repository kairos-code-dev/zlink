#include <zlink.h>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <cassert>

void log(const char* msg) {
    std::cout << "[TEST] " << msg << std::endl;
}

int main() {
    void *ctx = zlink_ctx_new();
    void *r1 = zlink_socket(ctx, ZLINK_ROUTER);
    void *r2 = zlink_socket(ctx, ZLINK_ROUTER);

    // Explicitly set identities
    zlink_setsockopt(r1, ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt(r2, ZLINK_ROUTING_ID, "ROUTER2", 7);

    // Bind R1
    int rc = zlink_bind(r1, "inproc://router_test");
    assert(rc == 0);
    log("R1 bound to inproc://router_test");

    // Connect R2
    rc = zlink_connect(r2, "inproc://router_test");
    assert(rc == 0);
    log("R2 connected to inproc://router_test");

    // Give time for connection (inproc connects immediately but good practice)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // --- TEST 1: R2 sends to R1 ---
    log("Sending from R2 -> R1...");
    // Frame 1: Dest ID ("ROUTER1")
    zlink_send(r2, "ROUTER1", 7, ZLINK_SNDMORE);
    // Frame 2: Data
    zlink_send(r2, "Hello", 5, 0);
    log("Sent.");

    char buf[256];
    log("R1 receiving...");
    // Frame 1: Source ID ("ROUTER2")
    int len = zlink_recv(r1, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R1 received frame 1 (ID): ") + buf).c_str());
    
    // Frame 2: Data ("Hello")
    len = zlink_recv(r1, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R1 received frame 2 (Data): ") + buf).c_str());

    if (strcmp(buf, "Hello") == 0) log("TEST 1 SUCCESS");
    else log("TEST 1 FAILED");

    // --- TEST 2: R1 replies to R2 ---
    log("Sending reply from R1 -> R2...");
    // Frame 1: Dest ID ("ROUTER2")
    zlink_send(r1, "ROUTER2", 7, ZLINK_SNDMORE);
    // Frame 2: Data
    zlink_send(r1, "World", 5, 0);
    log("Sent.");

    log("R2 receiving...");
    len = zlink_recv(r2, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R2 received frame 1 (ID): ") + buf).c_str());

    len = zlink_recv(r2, buf, 256, 0);
    buf[len] = 0;
    log((std::string("R2 received frame 2 (Data): ") + buf).c_str());

    if (strcmp(buf, "World") == 0) log("TEST 2 SUCCESS");
    else log("TEST 2 FAILED");

    zlink_close(r1);
    zlink_close(r2);
    zlink_ctx_term(ctx);
    return 0;
}
