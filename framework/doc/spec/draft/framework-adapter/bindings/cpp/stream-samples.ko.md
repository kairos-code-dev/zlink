[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM` 초안을 샘플로 보기 위한 문서다.

```cpp
class route_packet_session_t final : public packet_stream_session_t {
public:
    void on_packet(
      stream_t &stream,
      const message_t &header,
      const message_t &body) override
    {
    }
};

class route_raw_session_t final : public raw_stream_session_t {
public:
    void on_raw(
      stream_t &stream,
      const message_t &payload) override
    {
    }
};
```
