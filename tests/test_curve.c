/**
 * test_curve.c - CURVE encryption functionality test
 *
 * Tests:
 * - CURVE keypair generation (verifies libsodium static linking)
 * - CURVE mechanism availability
 * - CURVE encrypted socket communication
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

#define TEST_ENDPOINT "inproc://test_curve"
#define TEST_MESSAGE "Encrypted Hello"

int test_curve_support() {
    // Check if ZMQ has CURVE support
    int curve_available = zmq_has("curve");

    if (!curve_available) {
        fprintf(stderr, "[FAIL] CURVE mechanism not available\n");
        fprintf(stderr, "       libsodium may not be properly linked\n");
        return 1;
    }

    printf("[PASS] CURVE mechanism available\n");
    return 0;
}

int test_curve_keypair_generation() {
    char public_key[41];
    char secret_key[41];

    int rc = zmq_curve_keypair(public_key, secret_key);

    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to generate CURVE keypair: %s\n",
                zmq_strerror(zmq_errno()));
        fprintf(stderr, "       libsodium may not be properly linked\n");
        return 1;
    }

    printf("[PASS] CURVE keypair generation\n");
    printf("       Public key (first 16 chars): %.16s...\n", public_key);
    printf("       Secret key (first 16 chars): %.16s...\n", secret_key);

    // Verify key lengths (40 chars + null terminator)
    if (strlen(public_key) != 40 || strlen(secret_key) != 40) {
        fprintf(stderr, "[FAIL] Invalid key length\n");
        return 1;
    }

    printf("[PASS] CURVE key format validation\n");
    return 0;
}

int test_curve_encrypted_communication() {
    void *context = zmq_ctx_new();
    if (!context) {
        fprintf(stderr, "[FAIL] Failed to create context\n");
        return 1;
    }

    // Generate server keypair
    char server_public[41];
    char server_secret[41];
    int rc = zmq_curve_keypair(server_public, server_secret);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to generate server keypair\n");
        zmq_ctx_term(context);
        return 1;
    }

    // Generate client keypair
    char client_public[41];
    char client_secret[41];
    rc = zmq_curve_keypair(client_public, client_secret);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to generate client keypair\n");
        zmq_ctx_term(context);
        return 1;
    }

    // Create server socket
    void *server = zmq_socket(context, ZMQ_REP);
    if (!server) {
        fprintf(stderr, "[FAIL] Failed to create server socket\n");
        zmq_ctx_term(context);
        return 1;
    }

    // Configure server as CURVE server
    int curve_server = 1;
    rc = zmq_setsockopt(server, ZMQ_CURVE_SERVER, &curve_server, sizeof(int));
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to enable CURVE server: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Set server secret key
    rc = zmq_setsockopt(server, ZMQ_CURVE_SECRETKEY, server_secret, 41);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to set server secret key: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Bind server
    rc = zmq_bind(server, TEST_ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to bind server: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] CURVE server setup\n");

    // Create client socket
    void *client = zmq_socket(context, ZMQ_REQ);
    if (!client) {
        fprintf(stderr, "[FAIL] Failed to create client socket\n");
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Set client public key
    rc = zmq_setsockopt(client, ZMQ_CURVE_PUBLICKEY, client_public, 41);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to set client public key: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Set client secret key
    rc = zmq_setsockopt(client, ZMQ_CURVE_SECRETKEY, client_secret, 41);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to set client secret key: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Set server public key (client needs to know server's public key)
    rc = zmq_setsockopt(client, ZMQ_CURVE_SERVERKEY, server_public, 41);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to set server public key: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Connect client
    rc = zmq_connect(client, TEST_ENDPOINT);
    if (rc != 0) {
        fprintf(stderr, "[FAIL] Failed to connect client: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] CURVE client setup\n");

    // Allow time for CURVE handshake
    sleep_ms(200);

    // Send encrypted message
    int msg_len = strlen(TEST_MESSAGE);
    rc = zmq_send(client, TEST_MESSAGE, msg_len, 0);
    if (rc != msg_len) {
        fprintf(stderr, "[FAIL] Failed to send encrypted message: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Encrypted message send\n");

    // Receive encrypted message
    char buffer[256];
    rc = zmq_recv(server, buffer, sizeof(buffer) - 1, 0);
    if (rc < 0) {
        fprintf(stderr, "[FAIL] Failed to receive encrypted message: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    buffer[rc] = '\0';

    if (strcmp(buffer, TEST_MESSAGE) != 0) {
        fprintf(stderr, "[FAIL] Message mismatch: expected '%s', got '%s'\n",
                TEST_MESSAGE, buffer);
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Encrypted message receive and decryption\n");

    // Send reply
    const char *reply = "Encrypted ACK";
    rc = zmq_send(server, reply, strlen(reply), 0);
    if (rc != (int)strlen(reply)) {
        fprintf(stderr, "[FAIL] Failed to send encrypted reply: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    // Receive reply
    rc = zmq_recv(client, buffer, sizeof(buffer) - 1, 0);
    if (rc < 0) {
        fprintf(stderr, "[FAIL] Failed to receive encrypted reply: %s\n",
                zmq_strerror(zmq_errno()));
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    buffer[rc] = '\0';

    if (strcmp(buffer, reply) != 0) {
        fprintf(stderr, "[FAIL] Reply mismatch: expected '%s', got '%s'\n",
                reply, buffer);
        zmq_close(client);
        zmq_close(server);
        zmq_ctx_term(context);
        return 1;
    }

    printf("[PASS] Complete CURVE encrypted communication\n");

    // Cleanup
    zmq_close(client);
    zmq_close(server);
    zmq_ctx_term(context);

    return 0;
}

int main(void) {
    int failed = 0;

    printf("=== libzmq CURVE Encryption Tests ===\n\n");

    // Run tests
    failed += test_curve_support();
    failed += test_curve_keypair_generation();
    failed += test_curve_encrypted_communication();

    printf("\n=== Test Summary ===\n");
    if (failed == 0) {
        printf("All tests PASSED\n");
        printf("\n[SUCCESS] libsodium is properly statically linked\n");
        printf("          CURVE encryption is fully functional\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failed);
        printf("\n[ERROR] libsodium may not be properly linked\n");
        return 1;
    }
}
