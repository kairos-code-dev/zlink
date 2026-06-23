# C++ Monitoring E2E feature map

이 문서는 Config 7 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 구현한 시나리오

- `MON-Baseline`: 실제 PubSub 배포에서 message flow tracing을 `key_transitions`로 켜고,
  dispatch error evidence와 flow log가 생성되는지 검증한다. 이는 공통 README의 필수 관측 로그
  요건을 C++ E2E에서 고정한다.

## C++에서 제외한 시나리오

- `MON-A1`, `MON-A2`, `MON-A3`: C++ public monitoring builder는 socket/registry/spot source
  등록 API를 제공하지만, 현재 언어별 E2E 앱에는 각 source를 host와 colocate하고 event handler
  evidence로 노출하는 전용 monitoring host가 없다.
- `MON-A4`, `MON-A5`: C++ framework public API에는 runtime drain weight accessor가 없고,
  handshake 실패·timer stopped 상태를 안정적으로 유발하는 E2E trigger가 아직 없다.
- `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1`: kind 필터, 등록 검증, event handler 실패 격리,
  장애 반복 중 관측 연속성은 전용 monitoring host와 runtime error evidence가 필요하다.

비고: C++ unit tests는 monitoring builder/source 이름과 message-flow tracing gating을 검증한다.
이 E2E는 아직 monitoring source event payload까지 단언하지 않는다.
