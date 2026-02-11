#include "raw_client/asio_client.hpp"
#include "common/raw_framing.hpp"
#include <asio.hpp>
#include <cstring>
#include <cstdio>

namespace sample {

struct AsioClient::Impl {
    asio::io_context io_ctx;
    asio::ip::tcp::socket socket;
    uint8_t header_buf[4];
    std::vector<uint8_t> body_buf;

    Impl() : socket(io_ctx) {}
};

AsioClient::AsioClient(const std::string& name, const std::string& host, uint16_t port)
    : name_(name), host_(host), port_(port), impl_(std::make_unique<Impl>())
{}

AsioClient::~AsioClient() {
    disconnect();
}

bool AsioClient::connect() {
    try {
        asio::ip::tcp::resolver resolver(impl_->io_ctx);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        asio::connect(impl_->socket, endpoints);
        connected_ = true;

        // Start async read loop
        do_read_header();

        // Run IO context in background thread
        io_thread_ = std::thread([this]() { io_thread_func(); });

        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[%s] connect failed: %s\n", name_.c_str(), e.what());
        return false;
    }
}

void AsioClient::disconnect() {
    connected_ = false;
    if (impl_ && impl_->socket.is_open()) {
        asio::error_code ec;
        impl_->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
    }
    if (impl_) {
        impl_->io_ctx.stop();
    }
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

void AsioClient::send(const std::string& message) {
    auto frame = frame_encode(message);
    asio::error_code ec;
    asio::write(impl_->socket, asio::buffer(frame), ec);
    if (ec) {
        std::fprintf(stderr, "[%s] send error: %s\n", name_.c_str(), ec.message().c_str());
    }
}

void AsioClient::set_message_callback(MessageCallback cb) {
    callback_ = std::move(cb);
}

std::vector<std::string> AsioClient::drain_messages() {
    std::lock_guard<std::mutex> lock(msg_mutex_);
    std::vector<std::string> result(received_messages_.begin(), received_messages_.end());
    received_messages_.clear();
    return result;
}

void AsioClient::push_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(msg_mutex_);
    received_messages_.push_back(message);
}

bool AsioClient::is_connected() const {
    return connected_;
}

void AsioClient::io_thread_func() {
    try {
        impl_->io_ctx.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[%s] IO error: %s\n", name_.c_str(), e.what());
    }
    connected_ = false;
}

void AsioClient::do_read_header() {
    asio::async_read(impl_->socket, asio::buffer(impl_->header_buf, 4),
        [this](const asio::error_code& ec, size_t /*bytes_transferred*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted)
                    connected_ = false;
                return;
            }
            uint32_t length;
            std::memcpy(&length, impl_->header_buf, 4);
            length = ntohl(length);
            if (length > kMaxPayloadSize) {
                connected_ = false;
                return;
            }
            do_read_body(length);
        });
}

void AsioClient::do_read_body(uint32_t length) {
    impl_->body_buf.resize(length);
    asio::async_read(impl_->socket, asio::buffer(impl_->body_buf),
        [this](const asio::error_code& ec, size_t /*bytes_transferred*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted)
                    connected_ = false;
                return;
            }
            std::string msg(impl_->body_buf.begin(), impl_->body_buf.end());

            {
                std::lock_guard<std::mutex> lock(msg_mutex_);
                received_messages_.push_back(msg);
            }

            if (callback_) callback_(msg);

            // Continue reading
            do_read_header();
        });
}

} // namespace sample
