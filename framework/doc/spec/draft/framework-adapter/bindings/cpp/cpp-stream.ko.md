[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework C++ STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 1. 방향

`C++`에서도 `STREAM`은 packet session과 raw session 두 축으로 설명한다.
다만 다른 언어와 달리 host/runtime이 poll loop를 더 직접 소유한다는 점을 같이
적어야 한다.

## 2. Session

```cpp
class stream_t {
public:
    virtual ~stream_t() = default;
    virtual bool write(const message_t &payload) = 0;
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual void on_packet(
      stream_t &stream,
      const message_t &header,
      const message_t &body) = 0;
};

class raw_stream_session_t {
public:
    virtual ~raw_stream_session_t() = default;
    virtual void on_raw(stream_t &stream, const message_t &payload) = 0;
};
```

recv loop는 raw `stream_socket`을 직접 꺼내 쓰는 방식보다, host가 dispatch를 맡고
application은 session을 등록하는 모델을 기본으로 본다.
