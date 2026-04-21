[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM](./nestjs-stream.ko.md)

# Draft -- Node.js STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

- header serializer 선택 기준
- `ZLinkStream.write(...)`의 flush 시점과 backpressure 의미
- `onConnected`를 어떤 monitor 이벤트 기준으로 올릴지
- `onError`에 어떤 monitor 오류까지 매핑할지
