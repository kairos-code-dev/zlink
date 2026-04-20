[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM` 초안을 샘플로 보기 위한 문서다.

```cpp
class route_packet_handler_t final : public stream_packet_handler_t {
public:
    void handle(
      const message_t &header,
      const message_t &body,
      const stream_context_t &context) override
    {
    }
};

class route_raw_handler_t final : public stream_raw_handler_t {
public:
    void handle(
      const message_t &payload,
      const stream_context_t &context) override
    {
    }
};
```
