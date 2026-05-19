<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- C++ Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md) | [다음: Draft -- ZLink Framework C++ STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- C++ STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM`에서 아직 닫지 않은 항목을 모아 둔다.

framework MVP에서 `STREAM`은 packet 방식만 지원하고, Header는 framework가 정의한
`stream_header_t` 방식만 지원한다. raw stream session과 사용자 정의 Header framing은
미결 항목이 아니라 MVP 제외 범위다.

구현 전에 닫아야 할 항목은 아래와 같다.

- `stream_header_t` 필드의 정확한 wire 이름과 필수 여부
- Header 검증 실패 시 close, drop, error event 중 어떤 정책을 기본으로 둘지
- `stream_t::write_packet(...)`의 timeout과 backpressure 결과 타입
- host poll loop와 stream lifecycle 통합 방식
- `on_error(...)`에 어떤 monitor 오류까지 매핑할지
