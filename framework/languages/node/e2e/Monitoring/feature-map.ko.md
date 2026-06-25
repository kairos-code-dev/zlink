# Node Monitoring E2E feature map

이 문서는 Config 7 Runtime Monitoring 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- 없음.

## public contract gap

Node framework의 monitoring event 계약 타입은 public `@zlink-systems/framework`에서 export된다.
그러나 Node NestJS monitoring spec은 `monitoring: {...}` 등록, `ZLinkModule` 통합,
`ZLinkRuntimeEventHandler<TEvent>` provider 자동 discovery, timer handler failure 즉시 event 발행을
현재 **설계 모델 — 미구현**으로 명시한다. 현재 구현은 `runtime/diagnostics` 내부 source를 직접 쓰는
수준이므로, common Monitoring E2E를 통과시키기 위해 내부 source를 runner에서 직접 생성하지 않는다.

- `MON-A1`: 공통 시나리오는 service host의 public `monitoring.socket[...]` 등록이 실제 channel socket
  source에 붙고, `ZLinkRuntimeEventHandler<TEvent>` provider가 연결·해제 event를 받는지 본다. 현재
  NestJS monitoring 통합과 handler discovery가 미구현이므로 내부 socket diagnostics source를 runner에서
  직접 생성해 완료 처리하지 않는다.
- `MON-A2`: registry source는 public `monitoring.registry[...]` 등록, polling interval, event handler
  delivery가 모두 필요하다. 현재는 `runtime/diagnostics` snapshot source 수준만 있어
  `TopologyChanged`/`ServiceSummaryChanged` event delivery를 public NestJS 통합으로 검증할 수 없다.
- `MON-A3`: spot source는 public `monitoring.spot[...]` 등록과 spot/timer runtime event delivery가
  필요하다. 현재 NestJS 통합에는 timer handler failure를 즉시 monitoring event로 발행하는 public 경로가
  없으므로 완료 marker로 올리지 않는다.
- `MON-A4`: failover/drain 전이 관측은 socket/registry monitoring delivery가 먼저 필요하다. 또한 공통
  시나리오의 drain 절반은 runtime `Weight = 0`/restore 계약을 요구하지만, Node channel public API에는
  실행 중 provider weight를 바꾸는 drain/restore contract가 없다.
- `MON-A5`: handshake failure는 stream/channel handshake를 monitoring event로 올리는 public trigger와
  delivery 통합이 필요하고, registry/spot `StatusChanged`와 timer-stopped kind도 같은 monitoring
  dispatch 경로가 있어야 검증할 수 있다. 현재 내부 source만 직접 호출해 완료 처리하지 않는다.
- `MON-B1`: socket event kind filter는 `ZLinkSocketMonitoringRegistration.events` 계약 타입은 있으나,
  해당 registration을 live socket source에 붙이고 필터 결과를 handler evidence로 받는 public NestJS
  통합이 아직 없다.
- `MON-B2`: invalid monitoring 등록 검증은 public `monitoring: {...}` 등록 표면, source preflight,
  startup validator가 구현된 뒤에만 검증할 수 있다. 지금은 미구현 registration을 runner에서 가정하지
  않는다.
- `MON-C1`: event handler 실패 격리는 public event handler discovery, detached dispatch, runtime error
  sink reporting이 한 경로로 연결된 뒤 검증해야 한다. 내부 dispatcher를 직접 호출해 완료 처리하지 않는다.
- `MON-D1`: 장애·복구 반복 중 관측 연속성은 monitoring delivery 통합에 더해 provider restart/failover
  harness가 필요하다. event의 전역 순서나 무손실을 보장하지 않고, common 문서처럼 해당 전이 관측과
  monitoring task 생존만 public evidence로 고정해야 한다.
