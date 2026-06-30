# C++ RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

이 디렉터리는 기존 `Monitoring` 보조 runner를 대체하기 위한 Config 7 전용 포팅 위치다. 현재 slice는
service role에서 socket/spot monitoring event를 일부 수집하고, C++ public monitoring builder가 구성
단계와 framework 적용 단계에서 검증하는 등록 오류를 실행한다. C++ runtime source event가 아직 없는
항목은 trigger marker로 완료 처리하지 않고 gap으로 유지한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `MON-A1` | 구현 | trigger role이 service A로 transient profile request를 보내고, service role이 native socket monitor에서 발행되는 `Connected`, `ConnectionReady`, `Disconnected` event와 remote address evidence를 수집한다. |
| `MON-A2` | 구현 | standalone registry role이 native registry snapshot을 monitoring으로 연결하고, 원격 service start 뒤 `TopologyChanged`와 `ServiceSummaryChanged` evidence를 수집한다. |
| `MON-A3` | 구현 | service role이 registry-discovered TCP SPOT mesh peer를 구성하고, runtime이 native peer snapshot 변화로 발행한 `PeersChanged`, `/spot/create` 뒤 `SubjectsChanged`, failing timer의 `TimerHandlerFailed` event를 evidence로 수집한다. |
| `MON-A4` | 구현 | service drain/restore 중 public runtime option 변경으로 발행되는 socket `PeerAdmissionChanged` event, admin evidence, registry `TopologyChanged` evidence를 검증한다. |
| `MON-A5` | 구현 | trigger role이 invalid handshake를 service channel에 보내고, service role이 readiness 전에 끊긴 native socket monitor transition을 `HandshakeFailed`로 수집한다. registry role의 `StatusChanged`, service role의 spot `StatusChanged`, 실제 failing timer의 `TimerStoppedAfterUnhandledException` evidence도 함께 검증한다. |
| `MON-B1` | 구현 | filtered service role이 socket event kind filter를 `ConnectionReady`에 적용하고, 해당 event만 evidence에 남는지 검증한다. |
| `MON-B2` | 구현 | trigger role의 validation endpoint가 C++ public builder의 중복 socket source, 비양수 registry interval, missing spot/socket source framework 적용 검증을 실행하고 client가 결과를 단언한다. |
| `MON-C1` | 구현 | throwing service mode가 monitoring handler 예외를 발생시키고, runtime이 `monitoring-event-dispatch` stderr marker를 남기며 trigger role의 후속 messaging request가 계속 성공하는지 검증한다. |
| `MON-D1` | 구현 | runner가 filtered service를 `/shutdown`으로 중지한 뒤 같은 endpoint로 재시작하고, trigger role의 direct request, restarted service evidence, restart 이후 registry `TopologyChanged` continuity evidence를 검증한다. |

## 다음 구현 기준

- `.NET RuntimeMonitoring`의 registry, service, filtered service, throwing service, trigger, client 역할은
  C++ target으로 분리했고 log wait, validation, handshake failure endpoint를 사용한다.
- 기존 `Monitoring` runner는 PubSub flow log 보조 검증이므로 Config 7 완료 증거로 승격하지 않는다.
- public monitoring API로 수집할 수 없는 항목은 trigger-only marker로 메우지 않고 gap으로 유지한다.
