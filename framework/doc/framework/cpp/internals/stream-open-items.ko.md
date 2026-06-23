<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- C++ Stage Wrapper On SPOT](../spec/stage-wrapper-on-spot.ko.md) | [다음: C++ Stream Connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [STREAM](../spec/cpp-stream.ko.md)

# C++ STREAM Decisions

> 이 문서는 `C++` `STREAM`의 wire 의미와 host 동작 결정을 정리한다. 기준은
> `framework/languages/cpp` 구현이다.

framework core에서 `STREAM`은 packet 방식만 지원하고, Header는 framework가 정의한
`stream_header_t` 방식만 지원한다. raw stream session과 사용자 정의 Header framing은
core public 표면 밖의 extension 검토 범위다.

결정된 항목은 아래와 같다.

- STREAM wire header는 runtime 내부 표현으로 유지한다. C++ public session은
  `stream_dispatch_context_t`에서 packet name과 metadata를 읽고, request sequence와
  reply에 필요한 값은 runtime dispatch state가 보존한다.
- Header decode나 semantic validation에 실패한 packet은 application handler로 넘기지
  않는다. 현재 host는 이 실패를 `result_t` failure로 끝낸 뒤 예외를 삼키고
  `on_disconnected(...)`만 호출한다(`on_error` dispatch/monitoring publish 경로는 미연결).
- `stream_t::write_packet(...)`은 `stream_write_call_t`를 반환한다. 현재 write 경로는
  timeout 값을 저장만 하고 사용하지 않으며, 실패는 `disconnected`/writer 반환 실패로
  나타난다(별도 backpressure 처리 경로는 미구현).
- host lifecycle은 STREAM session lifecycle을 소유한다. session은 connect 시 scope를
  만들고, `on_disconnected(...)`와 binding cleanup이 끝난 뒤 scope를 닫는다.
- `on_error(...)`는 application handler 예외를 받지 않는다. monitor에서 관찰 가능한
  session-scoped transport 오류만 `stream_error_t`로 변환해 전달한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- C++ Stage Wrapper On SPOT](../spec/stage-wrapper-on-spot.ko.md) | [다음: C++ Stream Connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)
<!-- framework-adapter-nav:bottom:end -->
