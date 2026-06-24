# C++ Monitoring E2E feature map

이 문서는 Config 7 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 공통 외 보조 검증

- Monitoring runner는 실제 PubSub 배포에서 message flow tracing을 `key_transitions`로 켜고,
  dispatch error evidence와 flow log가 생성되는지 검증한다. 이 검증은 공통 Config 7의
  monitoring source event 시나리오 ID가 아니라, 공통 README의 필수 관측 로그 요건을 C++ E2E에서
  고정하는 보조 runner다.

## C++에서 제외한 시나리오

- `MON-A1`, `MON-A2`, `MON-A3`: C++ public monitoring builder는 socket/registry/spot source
  등록 API를 제공하고 unit test는 내부 monitoring runtime에 직접 payload를 넣어 handler 전달을
  검증한다. 하지만 현재 framework runtime에는 실제 channel, registry, spot source에서
  monitoring payload를 자동 발행하는 연결부가 없으므로 E2E 앱이 관측할 수 있는 runtime source
  event가 없다.
- `MON-A4`, `MON-A5`: C++ framework public API에는 runtime drain weight accessor가 없고,
  handshake 실패·timer stopped 상태를 안정적으로 유발하는 E2E trigger가 아직 없다.
- `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1`: kind 필터, 등록 검증, event handler 실패 격리,
  장애 반복 중 관측 연속성은 전용 monitoring host와 runtime error evidence가 필요하다.

비고: C++ unit tests는 monitoring builder/source 이름, 내부 runtime publisher 전달, message-flow
tracing gating을 검증한다. 이 E2E는 아직 monitoring source event payload까지 단언하지 않는다.
