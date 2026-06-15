<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Python Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md) | [다음: Draft -- ZLink Framework Python STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Python 묶음](./README.ko.md) | [STREAM](./fastapi-stream.ko.md)

# Draft -- Python STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

- header serializer 선택 기준
- `ZLinkStream.write(...)`는 async submit으로 본다. backpressure는 public
  non-blocking 옵션이 아니라 pending queue와 ready notification으로 처리한다.
- `on_connected`를 어떤 monitor 이벤트 기준으로 올릴지
- `on_error`에 어떤 monitor 오류까지 매핑할지

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Python Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md) | [다음: Draft -- ZLink Framework Python STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
