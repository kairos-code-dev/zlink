[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- C++ STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

- header serializer 선택 기준
- `stream_t::write(...)`는 async submit으로 본다. backpressure는 public
  non-blocking 옵션이 아니라 pending queue와 ready notification으로 처리한다.
- host poll loop와 stream lifecycle 통합 방식
- `on_error(...)`에 어떤 monitor 오류까지 매핑할지
