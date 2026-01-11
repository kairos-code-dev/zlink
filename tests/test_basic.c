/**
 * test_basic.c - Basic libzmq functionality test
 *
 * Tests:
 * - Library loading and version verification
 * - Basic socket creation and destruction
 * - REQ/REP pattern with message passing
 *
 * Returns 0 on success, 1 on failure
 */

#include <zmq.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

#define TEST_ENDPOINT "inproc://test_basic"
#define TEST_MESSAGE "Hello, ZeroMQ!"

int test_version() {
    int major, minor, patch;
    zmq_version(&major, &minor, &patch);

    printf("[TEST] ZMQ Version: %d.%d.%d\n", major, minor, patch);

    // Verify minimum version (4.x.x)
    if (major < 4) {
        fprintf(stderr, "[FAIL] ZMQ version too old: %d.%d.%d (expected 4.x.x)\n",
                major, minor, patch);
        return 1;
    }

    printf("[PASS] Version check\n");
    return 0;
}

int test_context_creation() {
    void *context = zmq_ctx_new();

    if (!context) {
        fprintf(stderr, "[FAIL] Failed to create ZMQ context: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("[PASS] Context creation\n");

    int rc = zmq_ctx_term(context);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to terminate context: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("[PASS] Context termination\n");
    return 0;
}

int test_socket_creation() {
    void *context = zmq_ctx_new();
    if (!context) {
        fprintf(stderr, "[FAIL] Failed to create context\n");
        return 1;
    }

    void *socket = zmq_socket(context, ZMQ_REQ);
    if (!socket) {
        fprintf(stderr, "[FAIL] Failed to create socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Socket creation\n");

    int rc = zmq_close(socket);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to close socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Socket close\n");

    zmq_ctx_term(context);
    return 0;
}

int test_req_rep_pattern() {
    void *context = zmq_ctx_new();
    if (!context) {
        fprintf(stderr, "[FAIL] Failed to create context\n");
        return 1;
    }

    // Create REP socket
    void *rep_socket = zmq_socket(context, ZMQ_REP);
    if (!rep_socket) {
        fprintf(stderr, "[FAIL] Failed to create REP socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_ctx_term(context);
        return 1;
    }

    // Bind REP socket
    int rc = zmq_bind(rep_socket, TEST_ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to bind REP socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] REP socket bind\n");

    // Create REQ socket
    void *req_socket = zmq_socket(context, ZMQ_REQ);
    if (!req_socket) {
        fprintf(stderr, "[FAIL] Failed to create REQ socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    // Connect REQ socket
    rc = zmq_connect(req_socket, TEST_ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to connect REQ socket: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] REQ socket connect\n");

    // Small delay to ensure connection is established
    sleep_ms(100);

    // Send message from REQ to REP
    int msg_len = strlen(TEST_MESSAGE);
    rc = zmq_send(req_socket, TEST_MESSAGE, msg_len, 0);
    if (rc != msg_len) {
        fprintf(stderr, "[FAIL] Failed to send message: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Message send\n");

    // Receive message on REP socket
    char buffer[256];
    rc = zmq_recv(rep_socket, buffer, sizeof(buffer) - 1, 0);
    if (rc < 0) {
        fprintf(stderr, "[FAIL] Failed to receive message: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    buffer[rc] = '\0';

    if (strcmp(buffer, TEST_MESSAGE) != 0) {
        fprintf(stderr, "[FAIL] Message mismatch: expected '%s', got '%s'\n",
                TEST_MESSAGE, buffer);
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Message receive and verification\n");

    // Send reply
    const char *reply = "ACK";
    rc = zmq_send(rep_socket, reply, strlen(reply), 0);
    if (rc != (int)strlen(reply)) {
        fprintf(stderr, "[FAIL] Failed to send reply: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    // Receive reply
    rc = zmq_recv(req_socket, buffer, sizeof(buffer) - 1, 0);
    if (rc < 0) {
        fprintf(stderr, "[FAIL] Failed to receive reply: %s\n", zmq_strerror(zmq_errno()));
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    buffer[rc] = '\0';

    if (strcmp(buffer, reply) != 0) {
        fprintf(stderr, "[FAIL] Reply mismatch: expected '%s', got '%s'\n",
                reply, buffer);
        zmq_close(req_socket);
        zmq_close(rep_socket);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] REQ/REP pattern complete\n");

    // Cleanup
    zmq_close(req_socket);
    zmq_close(rep_socket);
    zmq_ctx_term(context);

    return 0;
}

int main(void) {
    int failed = 0;

    printf("=== libzmq Basic Functionality Tests ===\n\n");

    // Run tests
    failed += test_version();
    failed += test_context_creation();
    failed += test_socket_creation();
    failed += test_req_rep_pattern();

    printf("\n=== Test Summary ===\n");
    if (failed == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failed);
        return 1;
    }
}
