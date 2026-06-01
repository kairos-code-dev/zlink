<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- C++ Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md) | [다음: Draft -- ZLink Stream Connector For C++](./cpp-stream-connector.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- C++ STREAM Decisions

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM`에서 `.NET` framework와 맞춰 닫은
> 결정을 정리한다.

framework core에서 `STREAM`은 packet 방식만 지원하고, Header는 framework가 정의한
`stream_header_t` 방식만 지원한다. raw stream session과 사용자 정의 Header framing은
core public 표면 밖의 extension 검토 범위다.

결정된 항목은 아래와 같다.

- `stream_header_t`는 `kind`, `codec`, `flags`, optional `request_seq`, `name`,
  `metadata`를 framework Header로 표현한다. C++ public view는 `packet_name`,
  `content_type`, `correlation_id` 같은 사용 편의 accessor를 제공할 수 있지만, wire
  의미는 `.NET`의 `ZlinkStreamHeader`와 맞춘다.
- Header decode나 semantic validation에 실패한 packet은 application handler로 넘기지
  않는다. session에 귀속할 수 있는 transport 오류는 `on_error(...)`로 올리고,
  handshake 실패처럼 session을 만들 수 없는 오류는 runtime monitoring에만 남긴다.
- `stream_t::write_packet(...)`은 `stream_write_call_t`를 반환한다. timeout,
  disconnected, backpressure 실패는 다른 framework call object와 같은 `result_t<void>`
  또는 `framework_exception_t` 규칙을 따른다.
- host lifecycle은 STREAM session lifecycle을 소유한다. session은 connect 시 scope를
  만들고, `on_disconnected(...)`와 binding cleanup이 끝난 뒤 scope를 닫는다.
- `on_error(...)`는 application handler 예외를 받지 않는다. monitor에서 관찰 가능한
  session-scoped transport 오류만 `stream_error_t`로 변환해 전달한다.
