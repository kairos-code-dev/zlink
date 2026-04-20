[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- Java STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

## 아직 닫지 않은 것

- header serializer 선택 기준
- body write surface를 `CompletionStage<Void>`로 둘지 별도 writer로 둘지
- connection open/close callback을 application 표면에 얼마나 노출할지
- backpressure와 flush 시점을 어떤 API로 설명할지

현재는 packet handler와 raw handler를 먼저 닫고, 이 항목들은 후속으로 남긴다.
