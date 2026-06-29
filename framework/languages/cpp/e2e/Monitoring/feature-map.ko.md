# C++ Monitoring E2E feature map

이 문서는 Config 7 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 공통 외 보조 검증

- Monitoring runner는 실제 PubSub 배포에서 message flow tracing을 `key_transitions`로 켜고,
  dispatch error evidence와 flow log가 생성되는지 검증한다. 이 검증은 공통 Config 7의
  monitoring source event 시나리오 ID가 아니라, 공통 README의 필수 관측 로그 요건을 C++ E2E에서
  고정하는 보조 runner다.

## product 구현 상태

C++ framework runtime은 channel, registry, spot source를 public monitoring builder에 연결하고,
runtime 변화가 monitoring payload로 자동 발행되도록 구현돼 있다. 이 문서의 제외 항목은 이제
product 기능 부재가 아니라 E2E 앱과 runner가 아직 해당 event를 공통 Config 7 marker로 단언하지
못한 항목이다.

## E2E/harness 대기 시나리오

- `MON-A1`, `MON-A2`, `MON-A3`: C++ public monitoring builder는 socket/registry/spot source
  등록 API와 runtime source 자동 발행 경로를 제공한다. 현재 E2E 앱은 이 event를 공통 Config 7
  marker로 수집하고 단언하는 runner 연결을 아직 갖추지 않았다.
- `MON-A4`: runtime drain 자체는 `channel_runtime_options_t`로 Resilience E2E에서 검증한다.
  다만 drain/failover 전이를 Config 7 event로 관측하는 E2E는 아직 없다.
- `MON-A5`: handshake 실패·timer stopped 상태를 안정적으로 유발하고 monitoring event로 관측하는
  E2E trigger가 아직 없다.
- `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1`: kind 필터, 등록 검증, event handler 실패 격리,
  장애 반복 중 관측 연속성은 전용 monitoring host와 runtime error evidence가 필요하다.

비고: C++ unit tests는 monitoring builder/source 이름, runtime source 자동 발행, 내부 runtime
publisher 전달, message-flow tracing gating을 검증한다. 이 E2E는 아직 monitoring source event
payload까지 단언하지 않는다.
