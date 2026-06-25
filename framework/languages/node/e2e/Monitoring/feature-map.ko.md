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

- `MON-A1`: socket source 등록과 event handler delivery를 하는 public NestJS monitoring 통합이 아직
  없어 완료 marker로 올리지 않는다.
- `MON-A2`: registry source polling과 `TopologyChanged`/`ServiceSummaryChanged` delivery를 public
  NestJS monitoring 통합으로 검증할 수 없어 완료 marker로 올리지 않는다.
- `MON-A3`: spot source polling과 timer failure event delivery를 public NestJS monitoring 통합으로
  검증할 수 없어 완료 marker로 올리지 않는다.
- `MON-A4`: failover/drain 전이 관측은 socket/registry monitoring 통합이 먼저 필요하다. 또한 Node
  channel public config에는 drain/restore weight runtime contract가 아직 없다.
- `MON-A5`: handshake failure, registry/spot status, timer-stopped kind 관찰은 public trigger와
  monitoring delivery 통합이 준비된 뒤 검증한다.
- `MON-B1`: socket event kind filter는 `ZLinkSocketMonitoringRegistration.events` 계약 타입은 있으나,
  해당 등록을 live socket source에 붙이는 public NestJS 통합이 아직 없다.
- `MON-B2`: invalid monitoring 등록 검증은 `monitoring: {...}` 등록 표면과 startup validator가
  구현된 뒤 검증한다.
- `MON-C1`: event handler 실패 격리는 public event handler discovery와 dispatch 경로가 구현된 뒤
  검증한다.
- `MON-D1`: 장애·복구 반복 중 관측 연속성은 monitoring delivery 통합과 restart/failover harness가
  준비된 뒤 검증한다.
