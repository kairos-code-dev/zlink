# C++ RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

이 디렉터리는 Config 7 RuntimeMonitoring 검증을 C++ framework public API로 실행한다.
현재 구현은 Redis location store를 공유하는 service, filtered service, throwing service, trigger,
client role로 구성된다. 별도 registry role은 없다. client role은 `.NET` baseline처럼 HTTP만
호출하고, framework channel request와 monitoring validation은 trigger/service role 내부 endpoint가
담당한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `MON-A1` | 구현 | trigger role이 service A로 transient profile request를 보내고, service role이 native socket monitor에서 발행되는 `Connected`, `ConnectionReady`, `Disconnected` event와 remote address evidence를 수집한다. |
| `MON-A2` | 구현 | runner가 svc-a 다음 svc-b를 시작해 peer location row를 실제로 바꾼다. svc-a의 `location-runtime` source가 낸 `TopologyChanged` payload에 `svc-b` node rid가 포함되고 `ServiceSummaryChanged`가 함께 발생하는지 확인한다. 이후 500ms 안정 구간에는 같은 snapshot event가 다시 발행되지 않아야 한다. |
| `MON-A3` | 구현 | service role이 Redis location store로 발견한 SPOT mesh peer를 연결하고, native peer snapshot 변화의 `PeersChanged`, `/spot/create` 뒤 `SubjectsChanged`, failing timer의 `TimerHandlerFailed` event를 evidence로 수집한다. |
| `MON-A4` | 구현 | runner가 `svc-a`를 강제 종료하고 같은 RID를 다른 channel endpoint로 재시작한다. 지속 discovery client가 이전 endpoint의 `Disconnected`와 새 endpoint의 `Connected`·`ConnectionReady`를 순서대로 수집하고, location payload의 ready RID·endpoint 교체도 확인한다. 이어서 drain/restore 각각이 새 `PeerAdmissionChanged`를 발생시키는지 검증한다. |
| `MON-A5` | 구현 | trigger role이 invalid handshake를 service channel에 보내고, service role이 `HandshakeFailed` 또는 대응 socket transition을 수집한다. location runtime `StatusChanged`, spot `StatusChanged`, 실제 failing timer의 `TimerStoppedAfterUnhandledException` evidence도 함께 검증한다. |
| `MON-B1` | 구현 | filtered service role이 socket event kind filter를 `ConnectionReady`에 적용하고, 해당 event만 evidence에 남는지 검증한다. |
| `MON-B2` | 구현 | trigger role의 validation endpoint가 C++ public builder의 중복 socket source, 비양수 location interval, missing spot/socket source framework 적용 검증을 실행하고 client가 결과를 단언한다. |
| `MON-C1` | 구현 | throwing service mode가 monitoring handler 예외를 발생시키고, runtime이 `monitoring-event-dispatch` stderr marker를 남기며 trigger role의 후속 messaging request가 계속 성공하는지 검증한다. |
| `MON-D1` | 구현 | runner가 filtered service를 두 번 강제 종료하고 같은 endpoint로 재시작한다. 지속 observer가 각 cycle의 location route down/up payload를 순서대로 수집하며, 마지막 재시작 뒤 request가 성공하는지 검증한다. |

## 유지 기준

- `run_e2e.sh`는 Redis-capable C++ build 디렉터리(`build-redis-vcpkg`)를 기본값으로 사용한다.
- Redis container는 loopback port로만 publish하고, scenario별 key prefix를 사용한다.
- service role은 SPOT mesh에 router endpoint와 pub/sub endpoint를 모두 공개한다. location store의
  SPOT peer row는 router endpoint를 기준으로 발견되고 pub endpoint는 metadata로 함께 전파된다.
- `run_e2e.sh`는 MON-A1/MON-B1/MON-D1에서 실제 message dispatch가 발생한 service와 trigger role의
  message-flow trace 파일을 필수 증거로 확인한다.
- 현재 MON-A1~MON-D1에는 public monitoring API gap이 없다. 이후 새 항목에서 public monitoring API로
  수집할 수 없는 항목이 나오면 trigger-only marker로 메우지 않고 별도 gap으로 기록한다.

## 검증

- 2026-07-15:
  - `framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-064803-2026580`
  - 의미: MON-A4의 같은 RID endpoint 교체와 socket·location 전이, MON-D1의 두 crash/restart
    cycle별 down/up 전이를 포함해 전체 scenario가 같은 gate에서 통과했다.
- 2026-07-08:
  - `timeout 560s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260708-133413-118111`
  - 의미: Redis location store 기반 RuntimeMonitoring role들이 MON-A1, MON-A2, MON-A3, MON-A4,
    MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 같은 gate에서 검증한다. 출력은
    `runtime-monitoring client result=passed`, `scenario MON-D1 passed`,
    `runtime-monitoring e2e result=passed`를 포함한다.
- 2026-07-03:
  - `framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260703-214231-52862`
  - 의미: Redis location store 기반 RuntimeMonitoring role들이 MON-A1, MON-A2, MON-A3, MON-A4,
    MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 같은 gate에서 검증한다.
