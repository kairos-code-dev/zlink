# Node Framework gap 수정 진행 log

이 log는
[`node-framework-spec-gap-ledger.ko.md`](../node-framework-spec-gap-ledger.ko.md)의
POSD·DDD 기반 review와 구현 검증 결과를 시간 순서로 기록한다. 이 파일의 기록은
완료 판정이 아니며, 최종 상태는 ledger의 항목과 검증 명령으로 판단한다.

## 2026-08-02

### 현재 상태

- `Nest` inbound dispatch builder가 호출자가 넘긴 options 객체를 계속 사용하도록 수정했다.
  따라서 HWM fluent 호출 뒤 `build()` 결과에 `applicationHwmBytes`와
  `processMemoryLimitBytes`가 보존된다.
- Native socket HWM getter에서 `bigint`를 `number`로 변환하도록 수정했다. 이 변환이 없으면
  runtime host 시작 중 `Math.max` 경로에서 `bigint`와 `number`를 섞어 처리한다.
- Channel envelope의 알 수 없는 `contentType`은 handler에 전달하기 전에
  `RequestProtocolError` 내부 원인으로 중단하도록 수정하고 회귀 검사를 추가했다.
- Node framework와 Redis location package의 binding 버전을 `11.1.0`으로 맞췄고,
  `verify_packaged_contract.sh`와 `npm ls @zlink-systems/zlink --all`을 통과시켰다.
- SPOT route physical slot은 native callback이 반환되기 전까지 유지하도록 수정했다.
  timeout 뒤 즉시 slot을 재사용하지 않는 회귀 경로를 확인했다.
- Node RouteMesh E2E client의 Client/Contract 코드에서 framework 직접 import를 제거했다.
  다만 Shared를 포함한 전이 의존성 검사와 실제 role/server adapter 경계 검증은 남아 있다.

### Sol Medium review 결과

Sol Medium review는 현재 candidate를 `NOT CLEAN`으로 판정했다.

1. public `ZLinkFrameworkException`이 41개의 상세 kind, numeric `code`,
   `isRetriable`과 join completion retry boolean을 노출한다. 공통 계약은 numeric 13종과
   `kind`만 허용한다.
2. `ZLinkInboundDispatchOptionValues`가 framework public barrel에 노출되어 있다.
   이 값 객체는 Nest/framework composition bridge의 private type이어야 한다.
3. unknown content type 검사가 direct decoder에만 있고 실제 dispatch handler의
   callback 횟수와 `ProtocolError` 결과를 검증하지 않는다.
4. `ChannelEgressRouting`과 `InstanceSpot` runner가 placeholder 상태이며 aggregate가
   partial stdout로 통과할 수 있다.
5. E2E client boundary 검사가 Client/Contract 직접 import만 확인하고, magic number와
   local duplicate DTO가 실제 public API 회귀를 가릴 수 있다.

### POSD·DDD 판단

- Error kind 변경은 public contract와 내부 lifecycle reason을 한 enum으로 유지하지 않는다.
  Application boundary에는 13종만 두고, actor·spot·routing bounded context의 상세 reason은
  내부 adapter에서 coarse kind로 변환해야 한다.
- Route request의 readiness는 test sleep으로 숨기지 않는다. one-way send는 source queue
  admission에서 완료되므로 remote handler 완료를 의미하지 않는다. route readiness와
  request submission의 책임 경계를 runtime queue에서 확인해야 한다.
- 새 public API나 test 전용 adapter를 추가하지 않는다. 기존 public surface로 해결되지 않는
  경우에는 계약 근거와 책임 소유자를 ledger에 먼저 남긴다.

### 다음 검증

1. public error kind와 내부 상세 reason을 분리한 뒤 generated declaration에 상세 kind,
   `code`, `isRetriable`이 남지 않는지 확인한다.
2. real dispatch 경로에서 malformed/unknown content type이 handler를 실행하지 않고
   callback을 한 번만 완료하는지 확인한다.
3. route one-way 뒤 request 순서를 fixed sleep 없이 반복 검증하고, route error가 발생하면
   public `Unavailable` 계약에 맞게 처리되는지 확인한다.
4. 전체 Node contract/runtime gate, package gate, process E2E를 다시 실행한다.

