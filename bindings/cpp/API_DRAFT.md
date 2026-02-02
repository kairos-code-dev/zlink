# C++ Wrapper API Draft (cppzmq-style)

## 공통 정책
- header-only, thin wrapper
- 기본은 에러 코드, `ZLINK_CPP_EXCEPTIONS` 정의 시 예외
- 모든 핸들은 move-only

---

## error helpers
```cpp
namespace zlink {

class error_t {
public:
    int code() const noexcept;
    const char* what() const noexcept;
};

inline error_t last_error();

#ifdef ZLINK_CPP_EXCEPTIONS
inline void throw_on_error(int rc);
#endif

} // namespace zlink
```

---

## message_t
```cpp
namespace zlink {

class message_t {
public:
    message_t();
    explicit message_t(size_t size);
    ~message_t();

    message_t(message_t&&) noexcept;
    message_t& operator=(message_t&&) noexcept;

    message_t(const message_t&) = delete;
    message_t& operator=(const message_t&) = delete;

    void* data() noexcept;
    const void* data() const noexcept;
    size_t size() const noexcept;
    bool more() const noexcept;
    int close() noexcept;

    zlink_msg_t* handle() noexcept;
    const zlink_msg_t* handle() const noexcept;

private:
    zlink_msg_t _msg;
    bool _valid;
};

} // namespace zlink
```

---

## context_t
```cpp
namespace zlink {

class context_t {
public:
    context_t();
    explicit context_t(int io_threads);
    ~context_t();

    context_t(context_t&&) noexcept;
    context_t& operator=(context_t&&) noexcept;

    context_t(const context_t&) = delete;
    context_t& operator=(const context_t&) = delete;

    void* handle() noexcept;

    int set(int option, const void* value, size_t len);
    int get(int option, void* value, size_t* len) const;

private:
    void* _ctx;
};

} // namespace zlink
```

---

## socket_t
```cpp
namespace zlink {

class socket_t {
public:
    socket_t(context_t& ctx, int type);
    ~socket_t();

    socket_t(socket_t&&) noexcept;
    socket_t& operator=(socket_t&&) noexcept;

    socket_t(const socket_t&) = delete;
    socket_t& operator=(const socket_t&) = delete;

    void* handle() noexcept;

    int bind(const char* endpoint);
    int bind(const std::string& endpoint);

    int connect(const char* endpoint);
    int connect(const std::string& endpoint);

    int close() noexcept;

    int send(const void* buf, size_t len, int flags = 0);
    int recv(void* buf, size_t len, int flags = 0);

    int send(message_t& msg, int flags = 0);
    int recv(message_t& msg, int flags = 0);

    int send(const std::string& s, int flags = 0);

    template <typename T>
    int set(int option, const T& value);

    template <typename T>
    int get(int option, T& value) const;

private:
    void* _socket;
};

} // namespace zlink
```

---

## poller_t (가능하면 C API poller 래핑)
```cpp
namespace zlink {

class poller_t {
public:
    poller_t();
    ~poller_t();

    poller_t(poller_t&&) noexcept;
    poller_t& operator=(poller_t&&) noexcept;

    poller_t(const poller_t&) = delete;
    poller_t& operator=(const poller_t&) = delete;

    int add(socket_t& s, short events, void* user = nullptr);
    int remove(socket_t& s);
    int wait(std::vector<poll_event>& events, long timeout_ms);

private:
    void* _poller;
};

} // namespace zlink
```

---

## 예외 모드
```cpp
#ifdef ZLINK_CPP_EXCEPTIONS
// send/recv/bind/connect에서 rc < 0이면 throw
#endif
```

---

## 사용 예시
```cpp
zlink::context_t ctx;
zlink::socket_t s(ctx, ZLINK_PAIR);
s.bind("ipc:///tmp/a");

zlink::message_t msg(5);
memcpy(msg.data(), "hello", 5);
s.send(msg);

zlink::message_t r;
s.recv(r);
```
