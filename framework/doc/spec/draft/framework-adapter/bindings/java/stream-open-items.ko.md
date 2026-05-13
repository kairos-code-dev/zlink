<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md) | [다음: ZLink Framework Spring Boot Monitoring](spring-boot-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- Java STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

## 아직 닫지 않은 것

- header serializer 선택 기준
- `ZLinkStream.write(...)`의 flush 시점과 backpressure 의미
- `onConnectedAsync(...)`를 어떤 monitor 이벤트 기준으로 올릴지
- `onErrorAsync(...)`에 어떤 monitor 오류까지 매핑할지

현재는 packet/raw session과 session lifecycle 방향을 먼저 닫고, 이 항목들은 후속으로
남긴다.
