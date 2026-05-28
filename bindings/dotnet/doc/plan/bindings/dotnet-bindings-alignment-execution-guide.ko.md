# .NET Bindings Core Alignment 실행 가이드

> 상태: 완료
> 기준 문서: `bindings/dotnet/plan/bindings/dotnet-bindings-core-alignment-plan.ko.md`
> 대상 범위: `bindings/dotnet/`, `bindings/dotnet/plan/bindings/`
> 목적: .NET bindings를 최신 `core` public surface에 맞춰 끝까지 정렬하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 플랜 문서의 내용을 실제 코드 변경 순서와 완료 판정 기준으로
고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 플랜 문서 하나로 고정한다.

- [`dotnet-bindings-core-alignment-plan.ko.md`](./dotnet-bindings-core-alignment-plan.ko.md)
  - 목적 / 기준 / 현재 상태:
    [`1. 목적`](./dotnet-bindings-core-alignment-plan.ko.md#1-목적),
    [`2. 입력 자료와 기준`](./dotnet-bindings-core-alignment-plan.ko.md#2-입력-자료와-기준),
    [`3. 현재 상태 요약`](./dotnet-bindings-core-alignment-plan.ko.md#3-현재-상태-요약)
  - 설계 원칙 / API 스타일 / 성능 원칙:
    [`4. 설계 원칙`](./dotnet-bindings-core-alignment-plan.ko.md#4-설계-원칙),
    [`4.1 .NET API 스타일 규칙`](./dotnet-bindings-core-alignment-plan.ko.md#41-net-api-스타일-규칙),
    [`4.4 성능 원칙`](./dotnet-bindings-core-alignment-plan.ko.md#44-성능-원칙),
    [`4.5 성능 민감 API 설계 규칙`](./dotnet-bindings-core-alignment-plan.ko.md#45-성능-민감-api-설계-규칙)
  - 고정 결정 / public surface:
    [`5.3 이번 작업에서 고정하는 결정`](./dotnet-bindings-core-alignment-plan.ko.md#53-이번-작업에서-고정하는-결정),
    [`5.7 raw socket public API 이름 정책`](./dotnet-bindings-core-alignment-plan.ko.md#57-raw-socket-public-api-이름-정책),
    [`5.7.1 Message public API 목표 shape`](./dotnet-bindings-core-alignment-plan.ko.md#571-message-public-api-목표-shape),
    [`5.8 Message/Socket ownership 계약`](./dotnet-bindings-core-alignment-plan.ko.md#58-messagesocket-ownership-계약),
    [`5.10 Service public API 목표 shape`](./dotnet-bindings-core-alignment-plan.ko.md#510-service-public-api-목표-shape),
    [`5.11 목표 public 타입 inventory`](./dotnet-bindings-core-alignment-plan.ko.md#511-목표-public-타입-inventory)
  - 단계별 구현 / 검증:
    [`6. 상세 실행 계획`](./dotnet-bindings-core-alignment-plan.ko.md#6-상세-실행-계획),
    [`테스트 전략 재정의`](./dotnet-bindings-core-alignment-plan.ko.md#테스트-전략-재정의),
    [`7. 권장 구현 순서`](./dotnet-bindings-core-alignment-plan.ko.md#7-권장-구현-순서),
    [`10. 최종 완료 정의`](./dotnet-bindings-core-alignment-plan.ko.md#10-최종-완료-정의),
    [`13. 착수 후 실행 명령`](./dotnet-bindings-core-alignment-plan.ko.md#13-착수-후-실행-명령),
    [`14. 파일별 실행 체크리스트`](./dotnet-bindings-core-alignment-plan.ko.md#14-파일별-실행-체크리스트),
    [`15. API review gate`](./dotnet-bindings-core-alignment-plan.ko.md#15-api-review-gate)

실행 중 설계 판단이 필요해 보이면 먼저 메인 플랜을 갱신하고, 그 다음 이 guide를
맞춘 뒤 코드를 수정한다. 코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`dotnet-bindings-core-alignment-plan.ko.md`](./dotnet-bindings-core-alignment-plan.ko.md)

이 가이드는 아래 내용을 메인 플랜에서 그대로 따른다.

- raw socket public API는 `Send` / `Receive` overload로 고정한다
- bytes/string 편의는 `Socket`이 아니라 `Message`가 담당한다
- callback ownership은 `managed copy 후 native close`로 고정한다
- public `routingId` 타입은 `string`으로 고정한다
- `Receiver`, `Timers`, `AttachStreamLen32Be`는 제거한다
- service 계층은 `Registry`, `Discovery`, `SpotNode`, `Spot`,
  `SocketMonitor`, `ServiceMonitor` 중심으로 재구성한다
- 검증 자산은 `samples`와 `tests`의 소수 contract 중심으로 재구성한다
- `.NET`스럽다는 이유로 hot path에 allocation/boxing/hidden copy를 늘리지 않는다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 플랜이다.
- 자동 실행이 필요하면 [`run_dotnet_bindings_alignment_execution.sh`](./run_dotnet_bindings_alignment_execution.sh)
  를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭
  루프이고, bindings 전용 정책은 이 guide와 메인 플랜이 결정한다.
- 기본 로그 디렉터리는 [`logs/`](./logs) 이다.
- 실행 wrapper 자체는 별도 `lock`을 두지 않는다.
  같은 작업을 병렬 실행해야 하면 `--logs-dir` 또는 `--gate-label`을 분리해서
  상태 파일 충돌을 피한다.

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 플랜만으로는 해결할 수 없는 `.NET` public surface 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/dotnet/`, `bindings/dotnet/plan/bindings/`만으로 해결할 수 없는 blocker

위 경우가 아니면:

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 contract test / sample 기준 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 guide 상태를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

이 가이드는 commit / push를 기본 규칙으로 강제하지 않는다.
commit / push는 사용자 지시가 있을 때만 수행한다.

병렬 실행 규칙:

- 기본값으로 두 개 이상 동시에 돌리지 않는다.
- 병렬 실행이 필요하면 실행 단위마다 `--logs-dir`을 분리한다.
- gate status 파일 충돌을 피하려면 `--gate-label`도 함께 분리한다.
- wrapper는 실행 자체를 막지 않는다.

## 4. 기본 실행 명령

메인 플랜이 고정한 기본 검증 흐름은 아래와 같다.

주의:

- 아래 명령은 최종 상태 기준의 전체 검증 흐름이다.
- 초기 slice에서는 아직 `samples/` 프로젝트가 존재하지 않을 수 있으므로
  이 섹션을 바로 전부 실행하지 않는다.
- 실제 진행 중에는 `7. slice별 최소 검증`의 현재 slice 명령을 먼저 사용하고,
  `Slice 4` 이후에만 전체 흐름을 순서대로 실행한다.

```bash
dotnet build /home/hep7/project/kairos/zlink/bindings/dotnet/Zlink.sln

dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj

dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairRecv/PairRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairCallback/PairCallback.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PubSubRecv/PubSubRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/SpotRecv/SpotRecv.csproj
```

실행 중 gate가 오래 걸리면 아래 명령으로 같은 셸에서 추적한다.

```bash
./core/tools/run_execution_gate_loop.sh --label dotnet_bindings_alignment_gate --count 1
```

스크립트 smoke 확인:

```bash
./bindings/dotnet/plan/bindings/run_dotnet_bindings_alignment_execution.sh --max-iterations 0
```

위 명령은 공통 supervisor까지 실제로 호출하지만 Codex iteration은 돌리지 않는
최소 점검 경로다. wrapper가 supervisor의 `max-iterations=0` 종료를 smoke 성공으로
해석하므로 종료 코드는 `0`이어야 한다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 Slice 1. 삭제와 native interop 재구축

메인 플랜 참조:

- `6. 상세 실행 계획`의 `Phase 0`, `Phase 1`
- `14. 파일별 실행 체크리스트`의 `14.1 삭제`, `14.2 Native 계층`

상태: `완료`

대상:

- `src/Zlink/Service/Receiver.cs`
- `src/Zlink/Timers.cs`
- `src/Zlink/Native/NativeMethods*.cs`
- `src/Zlink/Native/NativeTypes.cs`
- `src/Zlink/Native/NativeLibraryLoader.cs`
- `src/Zlink/Native/NativeServiceModels.cs`

작업:

- 최신 `libzlink.so` export와 `core/include/zlink.h` 기준으로 P/Invoke 전면 재정렬
- legacy `zlink_receiver_*`, `setsockopt/getsockopt`, old stream helper,
  old spot pub/sub, old poller entrypoint 제거
- native struct / enum mirror와 loader 검증 로직 재정리
- `NativeMethods`를 역할별 partial 파일 구조로 고정
- 최신 service / monitor / topology native model 선언을 별도 파일로 분리

진행 메모:

- `NativeMethods*.cs`에서 legacy `zlink_stream_attach_len32be`,
  `zlink_stream_send`, `zlink_stream_send_msg`, `zlink_timers_*`,
  old socket monitor recv/open 선언이 제거됨
- stale `zlink_socket_peer_*` P/Invoke와 이를 감싼 `Socket` peer 조회 surface가
  제거되어 현재 `libzlink.so` missing export 참조가 줄어듦
- `Socket.OpenMonitor`가 최신 `zlink_socket_monitor_open(options)` 경로로 정렬됨
- legacy `AttachStreamLen32Be`는 남은 public surface 정리 전까지 managed LEN32BE
  fallback으로 유지되어 native missing export 의존이 제거됨
- legacy monolithic `NativeMethods.cs`가 shared scaffold로 축소되고
  actual P/Invoke 선언이 `Core/Socket/Monitor/Service/Poller` partial로 재배치됨
- `Message`의 legacy native property setter/getter (`zlink_msg_get/set`) 의존이 제거됨
- `NativeTypes.cs`에서 최신 header에 없는 stale `ProviderInfo` / `PeerInfo`
  mirror와 helper가 제거됨
- `dotnet build`와 `dotnet test --filter "FullyQualifiedName~stream_callback_echo_len32be"`
  green 확인 완료
- `dotnet build` green 재확인 완료
- native export diff는 intentionally-unbound helper
  (`zlink_poller_wait`, `zlink_subscription_at`, `zlink_xpub_recv_part`,
  `zlink_thread_start`, `zlink_thread_join`)만 남고, missing export 참조는 0건으로 정리됨

완료 기준:

- 존재하지 않는 native export를 참조하는 선언이 남지 않음
- `dotnet build`가 native interop 레벨 오류 없이 통과
- 최신 export / 선언 diff가 설명 가능한 상태로 줄어듦

### 5.2 Slice 2. core facade와 성능 민감 hot path 정렬

메인 플랜 참조:

- `6. 상세 실행 계획`의 `Phase 2`, `Phase 4`
- `5.7 raw socket public API 이름 정책`
- `5.7.1 Message public API 목표 shape`
- `5.8 Message/Socket ownership 계약`
- `4.4 성능 원칙`, `4.5 성능 민감 API 설계 규칙`
- `14. 파일별 실행 체크리스트`의 `14.3 Core facade`, `14.5 부가 API`

상태: `완료`

대상:

- `src/Zlink/Message.cs`
- `src/Zlink/Socket.cs`
- `src/Zlink/Monitor.cs`
- `src/Zlink/Poller.cs`
- `src/Zlink/Enums.cs`
- `src/Zlink/SocketOptions.cs`
- `src/Zlink/SocketOptionValidation.cs`
- `src/Zlink/RoutingIdCodec.cs`
- `src/Zlink/Context.cs`
- `src/Zlink/Errors.cs`
- `src/Zlink/Runtime.cs`
- `src/Zlink/AtomicCounter.cs`
- `src/Zlink/ZlinkStopwatch.cs`

작업:

- `Socket`을 `Send` / `Receive` overload 중심으로 재구성
- topic 의미가 있는 raw socket (`PUB`/`SUB`/`XPUB`/`XSUB`)은
  `Publish` / `Subscribe` / subscription event surface로 정리
- `Message`를 payload convenience + ownership 타입으로 재정리
- raw socket callback을 `RecvHandler` / `SendReadyHandler`로 정리하고
  `EBUSY` 제약을 public surface에 반영
- `SocketMonitor`를 `AttachHandler` / `Receive` / `Snapshot` / `Close`
  shape로 맞춤
- callback ownership을 `managed copy 후 native close`로 통일
- `AttachStreamLen32Be`를 public surface에서 제거하고 framing helper는
  `samples/` helper로만 남김
- `StreamSend` 같은 stream 전용 public 이름을 제거하고 raw socket
  `Send` / `Receive` + public `routingId` string 계약으로 수렴
- generic poller만 남기고 old receiver/spot 전용 poller entrypoint 제거
- `.NET`스럽다는 이유로 hot path에 `IEnumerable`, LINQ, serializer convenience,
  per-call delegate allocation을 넣지 않도록 구현 구조 고정

진행 메모:

- `NativeMethods*.cs`에서 legacy `zlink_receiver_*`, `zlink_spot_pub_*`,
  `zlink_spot_sub_*`, old poller `spot/receiver` entrypoint가 제거됨
- legacy `zlink_setsockopt` / `zlink_getsockopt` 선언이 제거됨
- `Socket`에 public `routingId` string 기반 `Send` / `Receive` overload가 추가되고
  single-part `Receive(out Message)` 경로가 그 overload 위로 정렬됨
- `Monitor.cs`가 `SocketMonitor` / `SocketMonitorEvent` 이름으로 이동하기 시작했고
  관련 `stream` tests가 새 routing-id 수신 모델 기준으로 갱신 중임
- `Registry` / `Discovery` / `SpotNode`의 구형 public surface가 정리되어
  `Bind`, fixed service view, topology 전용 node 역할만 남김
- `Message`에 `RefCount`, UTF-8 string convenience가 반영됨
- `ContextOption.Blocky`가 public enum에 반영됨
- `Context` contract test와 routing id option roundtrip이 최신 native 계약으로 복구됨
- `Socket`에 raw `RecvHandler` / `SendReadyHandler`가 추가되고
  callback ownership이 managed copy 후 native close 경로로 정리됨
- `SocketMonitor`에 `AttachHandler`, `Snapshot`, `Close`가 추가되고
  `MonitorStatus` value type이 도입됨
- buffer 기반 raw `Socket.Send/Receive` public overload가 제거되고
  `Message` 중심 public surface로 수렴함
- `Socket` topic path는 `Send` / `Receive` 강제 대신
  `Publish` / `Subscribe` / `SubscribeHandler` / subscription event 경로로
  분리되어 `PUB`/`SUB` 계열 `ENOTSUP` 누수를 막도록 계속 정리 중임
- `test_pubsub.cs`, `test_xpub_verbose.cs`는 새 `Socket` topic contract
  (`SetSubscription`, `Publish`, `Subscribe`, subscription event) 기준으로
  갱신되고 해당 필터 13건 green 확인 완료
- `message|socket|stream|ctx|pair` 대상 회귀 51건이
  `stream_maxmsgsize_disconnects_oversized_payload` 제외 조건에서 green 확인됨
- `.NET` send 경로가 C++ binding과 같은 `move + failure restore` ownership
  모델로 정렬되어 success 시 ownership 소비, failure 시 caller ownership 유지
  계약이 코드에 직접 드러나게 됨
- `test_pair_tcp.cs`에 raw multipart `PAIR` roundtrip 회귀가 추가되어
  `Socket.Send(IReadOnlyList<Message>)` public contract가 고정됨
- `test_router_multiple_dealers.cs`의 ROUTER reply 경로가 public `routingId`
  string 기준으로 green 복구됨
- `bindings/dotnet/runtimes/linux-x64/native/libzlink.so`를 현재
  `core/build/lib/libzlink.so`와 다시 맞춰 stale runtime bundle 때문에 남아 있던
  blocking multipart/routed send `EINVAL`이 제거됨
- `Poller`는 raw socket + fd 전용 shape로 유지되고 기존 pair poller 회귀가 계속 green임
- `Runtime`, `AtomicCounter`, `ZlinkStopwatch` 기본 interop contract가
  `test_system.cs`로 확인됨
- library public surface에서 `AttachStreamLen32Be`와 `StreamSend`가 제거되고
  stream callback routing id가 public string 계약으로 정렬되기 시작함
- `test_stream_socket.cs`는 test-side LEN32BE helper로 이행되어 stream contract
  대부분이 계속 검증 가능함
- `dotnet test --filter "(FullyQualifiedName~message|FullyQualifiedName~socket|FullyQualifiedName~stream|FullyQualifiedName~ctx)&FullyQualifiedName!=Zlink.Tests.test_stream_socket.stream_maxmsgsize_disconnects_oversized_payload"`
  green 확인 완료
- `dotnet test --filter "FullyQualifiedName~router_multiple_dealers|FullyQualifiedName~pair_multipart_roundtrip"`
  green 확인 완료
- `stream_maxmsgsize_disconnects_oversized_payload`는 callback 기반 bounded wait로
  정리되어 full `dotnet test` green을 다시 막지 않게 됨

완료 기준:

- raw socket / monitor / poller public API가 메인 플랜과 일치
- `Socket`과 `Message`의 책임 경계가 코드와 문서에 모두 드러남
- LEN32BE framing helper와 stream 전용 public 이름이 library surface에서 제거됨
- 대표 send/receive 경로에 숨은 allocation/copy가 들어가지 않도록 구조가 정리됨

### 5.3 Slice 3. service facade 재구성

메인 플랜 참조:

- `6. 상세 실행 계획`의 `Phase 3`
- `5.10 Service public API 목표 shape`
- `5.11 목표 public 타입 inventory`
- `14. 파일별 실행 체크리스트`의 `14.4 Service facade`

상태: `완료`

대상:

- `src/Zlink/Service/Registry.cs`
- `src/Zlink/Service/Discovery.cs`
- `src/Zlink/Service/Spot.cs`
- `src/Zlink/Service/ServiceMonitor.cs`
- `src/Zlink/Service/RegistryQueryClient.cs`
- `src/Zlink/Service/TopologyModels.cs`
- `src/Zlink/Socket.cs`의 `AttachDiscovery(Discovery)` 경로

작업:

- `Registry`를 `Bind` / snapshot / topology query 모델로 전환
- `Discovery`를 `serviceType + serviceName` 고정 view로 전환
- `Spot`을 unified handle로 정리하고 recv/callback 모델 둘 다 최신 계약에 맞춤
- `ServiceMonitor`와 topology models 추가
- attach discovery 이후 lifecycle 제약을 예외/문서에 반영

진행 메모:

- `Registry`에 `Bind`, snapshot/topology/member query entrypoint가 추가됨
- `Discovery`에 `serviceType + serviceName`, metadata/value/member peer, `OpenMonitor`가 추가됨
- `ServiceMonitor`, `RegistryQueryClient`, `TopologyModels`,
  `Socket.AttachDiscovery`, `SpotNode.AttachDiscovery/Snapshot/Peers/Subjects`
  기반이 추가됨
- `ServiceType`가 native `service_type` (`SPOT` / `SOCKET`) 값으로 정렬되고
  monitor/topology/summary event 계층은 별도 `ServiceKind` 모델로 분리됨
- `Spot`이 split `spot_pub/sub`에서 unified `zlink_spot_new` 기반 public shape로 전환됨
- `Spot`에 `Publish`, `SetSubscription`, `UnsetSubscription`,
  `SubscribeHandler`, `SendReadyHandler`, `OpenMonitor`가 반영됨
- `Discovery.OpenMonitor`와 `Spot.OpenMonitor` 기본 event mask가
  native가 허용하는 discovery/sub-side service monitor mask로 정리됨
- `Registry.SetEndpoints/Start`, `DiscoveryServiceType`, `ReceiverInfoRecord`,
  `SpotNode`의 register/tls/default socket/legacy option surface가 제거됨
- `dotnet test --filter "FullyQualifiedName~spot"`은 green으로 복구됨
- `dotnet test --filter "FullyQualifiedName~spot|FullyQualifiedName~service|FullyQualifiedName~topology|FullyQualifiedName~discovery"` green 확인 완료
- `test_service_monitor_contract.cs`, `test_topology_contract.cs`가 추가되어
  service monitor open/close shape와 empty topology/query contract가 고정됨
- `test_attach_discovery_contract.cs`가 추가되어 discovery attach 이후
  manual peer lifecycle 제약이 wrapper contract로 고정됨
- `Socket.AttachDiscovery(Discovery)`와 `SpotNode.AttachDiscovery(Discovery)`에
  discovery-owned lifecycle XML-doc가 추가됨

완료 기준:

- `Receiver` 없이도 service 계층 public surface가 최신 문서와 맞음
- topology / metadata / service monitor 흐름이 contract test 가능한 수준으로 정리됨
- service 계층이 raw socket 계약과 충돌하지 않음

### 5.4 Slice 4. tests, samples, packaging, migration 마감

메인 플랜 참조:

- `테스트 전략 재정의`
- `6. 상세 실행 계획`의 `Phase 5`, `Phase 6`
- `13. 착수 후 실행 명령`
- `14. 파일별 실행 체크리스트`의 `14.6 Tests` 이후 전부
- `15. API review gate`

상태: `완료`

대상:

- `tests/Zlink.Tests/*`
- `samples/*`
- `samples/Zlink.Samples.sln`
- `samples/run_samples.sh`
- `samples/run_samples.ps1`
- `src/Zlink/Zlink.csproj`
- `Zlink.sln`
- breaking change / migration 관련 문서

작업:

- core 포팅성 테스트를 줄이고 wrapper 전용 contract test만 남김
- 패턴별 `recv` / `callback` sample project 추가
- 각 sample project에 짧은 README 추가
- solution / csproj wiring 정리
- breaking change와 migration note 정리

진행 메모:

- `test_spot_pubsub_basic.cs`가 unified `Spot` contract 기준으로 재작성됨
- `dotnet test --filter "FullyQualifiedName~spot"` green 확인 완료
- `dotnet test --filter "FullyQualifiedName~test_pubsub|FullyQualifiedName~test_xpub_verbose"`
  green 확인 완료
- `test_callback_contract.cs`, `test_monitor_contract.cs`,
  `test_attach_discovery_contract.cs`가 추가되어 callback/monitor/discovery
  lifecycle contract가 얇은 wrapper 검증으로 분리됨
- `samples/Zlink.Samples.sln`, `samples/SampleCommon/`, 11개 sample project,
  sample별 README, `run_samples.sh`, `run_samples.ps1`가 추가됨
- `dotnet build bindings/dotnet/samples/Zlink.Samples.sln` green 확인 완료
- `./bindings/dotnet/samples/run_samples.sh` green 확인 완료
- `doc/bindings/dotnet.ko.md`, `doc/bindings/dotnet.md`에 최신 public surface와
  breaking change / migration note가 반영됨
- `./bindings/dotnet/plan/bindings/run_dotnet_bindings_alignment_execution.sh --max-iterations 0`
  smoke green 확인 완료

완료 기준:

- `dotnet test` green
- 대표 sample smoke green
- API review gate 질문에 모두 `예`로 답할 수 있음

## 6. 실행 루프 규칙

매 iteration에서 아래 순서를 지킨다.

1. guide에서 가장 먼저 `미착수` 또는 `진행중`인 slice 하나만 잡는다.
2. 해당 slice와 직접 연결된 메인 플랜 section만 다시 읽는다.
3. 코드 수정 전에 삭제 대상과 목표 public shape를 다시 확인한다.
4. 코드 수정 후에는 그 slice의 최소 검증을 즉시 실행한다.
5. 검증이 끝나면 guide 상태를 `진행중` 또는 `검증중` 또는 `완료`로 갱신한다.
6. slice가 `완료`가 아니면 다음 slice로 넘어가지 않는다.

금지:

- 메인 플랜에 없는 compatibility surface를 임의로 추가
- `Socket`에 `byte[]` / `string` / serializer convenience overload 추가
- `Receiver`, `Timers`, LEN32BE helper를 되살림
- hot path 비용이 큰 convenience를 public surface에 고정

## 7. slice별 최소 검증

### 7.1 Slice 1 검증

```bash
dotnet build /home/hep7/project/kairos/zlink/bindings/dotnet/Zlink.sln
```

추가 확인:

- native export와 P/Invoke 선언이 구조적으로 맞는지 확인
- 삭제 대상으로 합의된 old declaration이 남지 않았는지 확인

### 7.2 Slice 2 검증

```bash
dotnet build /home/hep7/project/kairos/zlink/bindings/dotnet/Zlink.sln
dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj --filter "(FullyQualifiedName~message|FullyQualifiedName~socket|FullyQualifiedName~stream|FullyQualifiedName~ctx)&FullyQualifiedName!=Zlink.Tests.test_stream_socket.stream_maxmsgsize_disconnects_oversized_payload"
```

추가 확인:

- callback attach 후 direct recv/poll `EBUSY` 계약 확인
- socket monitor callback/snapshot/close shape 확인
- `Message`/`Socket` ownership 설명이 public API와 충돌하지 않는지 확인

### 7.3 Slice 3 검증

```bash
dotnet build /home/hep7/project/kairos/zlink/bindings/dotnet/Zlink.sln
dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj --filter "FullyQualifiedName~spot|FullyQualifiedName~service|FullyQualifiedName~topology|FullyQualifiedName~discovery"
```

추가 확인:

- attach discovery lifecycle 제약 확인
- service monitor / snapshot marshalling 확인

### 7.4 Slice 4 검증

```bash
dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj

dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairRecv/PairRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PairCallback/PairCallback.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/PubSubRecv/PubSubRecv.csproj
dotnet run --project /home/hep7/project/kairos/zlink/bindings/dotnet/samples/SpotRecv/SpotRecv.csproj
```

추가 확인:

- public API review gate 질문을 다시 통과하는지 확인

## 8. 완료 판정

아래를 모두 만족해야 종료할 수 있다.

1. guide의 모든 slice 상태가 `완료`다.
2. 메인 플랜의 `10. 최종 완료 정의`를 만족한다.
3. `14. 파일별 실행 체크리스트`가 실질적으로 해소되었다.
4. `15. API review gate` 질문에 모두 `예`라고 답할 수 있다.
5. 남은 작업이 없다면 최종 응답은 정확히 `미적용 사항이 없습니다.`다.

추가 작업이 남았지만 같은 루프에서 계속 진행 가능하면 최종 응답은
정확히 `계속 진행 필요`로 남긴다.

사용자 판단이나 설계 갱신이 필요하면 최종 응답은 아래 형식을 쓴다.

```text
사용자 입력 필요: <짧고 구체적인 이유>
```
