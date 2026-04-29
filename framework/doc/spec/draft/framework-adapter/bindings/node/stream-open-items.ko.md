[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM](./nestjs-stream.ko.md)

# Draft -- Node.js STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

- header serializer 선택 기준
- `ZLinkStream.write(...)`는 async submit으로 본다. backpressure는 public
  non-blocking 옵션이 아니라 pending queue와 ready notification으로 처리한다.
- `onConnected`를 어떤 monitor 이벤트 기준으로 올릴지
- `onError`에 어떤 monitor 오류까지 매핑할지
