#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <deque>
#include <cstdint>

namespace sample {

using MessageCallback = std::function<void(const std::string& message)>;

class AsioClient {
public:
    AsioClient(const std::string& name, const std::string& host, uint16_t port);
    ~AsioClient();

    // Connect to the server (blocking)
    bool connect();

    // Disconnect
    void disconnect();

    // Send a raw message string (will be length-prefixed)
    void send(const std::string& message);

    // Set callback for received messages (called from IO thread)
    void set_message_callback(MessageCallback cb);

    // Drain received messages (thread-safe, call from main thread)
    std::vector<std::string> drain_messages();

    // Push a message back into the receive queue (thread-safe)
    void push_message(const std::string& message);

    // Check if connected
    bool is_connected() const;

    const std::string& name() const { return name_; }

private:
    void io_thread_func();
    void do_read_header();
    void do_read_body(uint32_t length);

    std::string name_;
    std::string host_;
    uint16_t port_;

    // Pimpl to keep asio out of the header
    struct Impl;
    std::unique_ptr<Impl> impl_;

    MessageCallback callback_;

    std::mutex msg_mutex_;
    std::deque<std::string> received_messages_;

    std::thread io_thread_;
    bool connected_ = false;
};

} // namespace sample
