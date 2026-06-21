<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 등록·codec](config-4-registration-codec.ko.md) | [다음: Discovery·Registry HA](config-6-discovery-registry-ha.ko.md)
<!-- framework-adapter-nav:end -->

# Config 5 — Resilience·lifecycle 배포

다중 노드 + registry 배포를 띄운 뒤, 프로세스를 실제로 죽이고 다시 띄우며 복구·수명·정리가
의도대로 도는지 검증한다. 비용이 큰 시나리오가 많아 대부분 `P1`·`P2`다.

## 1. 목적과 범위

- 다룬다: 프로세스 restart·재스케줄, client reconnect, in-flight 중 crash, cancellation, graceful shutdown, resource·stale 정리, network/registry partition 복구, rolling·blue-green 전환, 실패 중 관측.
- 범위 밖: 정상 경로 messaging/resolve(Config 1·2), codec(Config 4).

## 2. 서버 구성 (한 번 구동 + 동적 조작)

| 역할 | 수 | 구성 |
|------|----|------|
| registry | 1 | discovery server. partition 시나리오에서 격리 대상. |
| provider | 2~3 | Config 1과 같은 channel provider. 시나리오가 죽이고/재시작/교체한다. |
| consumer | 시나리오별 | 지속 트래픽을 보내며 복구를 관측한다. |

스크립트가 기본 배포를 띄운 뒤, 시나리오별로 provider/registry 프로세스를 SIGTERM·SIGKILL로
종료하거나 새 endpoint로 재기동한다.

## 3. 실행 모델

`run_e2e.sh`가 배포를 띄우고, 각 시나리오 client가 트래픽을 보내는 동안 스크립트가 프로세스를
조작한다.

**하네스 전제(필수 확장):** 이 config는 실행 중 프로세스 제어가 필요하다. 현재 ScenarioE2E
runner는 시작-후-cleanup-종료만 지원하므로, 아래 시나리오를 실행하려면 harness에 명시적
`stop(SIGTERM)`·`kill(SIGKILL)`·`restart`·(필요 시 `pause/resume`) 연산을 먼저 추가해야 한다.
이 연산이 없으면 해당 시나리오는 "미구현(하네스 대기)"로 둔다.

**성공 기준 어휘:** "정해진 public error"는 시나리오마다 정확한 `ZLinkFrameworkErrorKind`
(`RouteNotConnected`·`RequestTargetNotFound`·`RequestRejected`·`RequestFailed`) 또는
`TimeoutException`과, 그 retriable 여부·timeout window를 명시한다. 재시도가 framework 동작인지
client harness 동작인지도 구분한다. 복구는 "이후 follow-up request 성공 + topology query에서
제거/추가 반영"처럼 **관측 가능한 결과**로 본다(내부 pending dict는 public 표면이 아니므로
직접 단언하지 않는다).

## 4. 시나리오

### Track A — restart와 재연결

#### RL-A1 server restart

우선순위: `P1`

- 절차: provider를 종료했다가 같은 endpoint로 재시작한다. consumer는 계속 request를 보낸다.
- 검증: 다운 구간엔 정해진 public error/재시도, 복구 뒤 request 정상화. consumer 재시작 없음.
- 세부 동작: 재기동 복구.

#### RL-A2 Kubernetes식 pod 재스케줄

우선순위: `P2`

- 절차: provider를 죽이고 다른 endpoint·같은 rid로 새로 띄운다. (e2e server는 `ConfigureServerRouting().RoutingId`를 노출하는 routing-id 구성 옵션이 있어야 대체 provider가 같은 rid로 광고할 수 있다.)
- 검증: registry가 rid를 새 endpoint로 갱신하고, consumer가 stale로 가지 않는다. (정상 경로는 Config 1 RM-A4, 여기선 in-flight·반복 복구 관점)
- 세부 동작: 재스케줄 복구.

#### RL-A3 client reconnect storm

우선순위: `P1`

- 절차: 다수 client가 동시에 끊겼다가 한꺼번에 재접속한다.
- 검증: 재접속 폭주에도 server가 안정적으로 수용하고 messaging이 정상화된다.
- 세부 동작: 대량 재연결 안정성.

#### RL-A4 rolling update / blue-green 전환

우선순위: `P2`

- 절차: provider를 한 대씩 새 버전으로 교체(rolling)하거나, 새 set으로 일괄 전환(blue-green)한다.
- 검증: 전환 중에도 무중단으로 request가 처리되고, 전환 완료 뒤 신규 set으로만 routing된다.
- 세부 동작: 무중단 배포 전환.

#### RL-A5 provider flapping

우선순위: `P2`

- 절차: provider 하나를 짧은 간격으로 down/up 반복(flapping)시키며 consumer가 request를 계속 보낸다.
- 검증: flapping 중에도 consumer가 살아 있는 provider로 안정적으로 수렴하고, stale endpoint로 반복 timeout 하지 않는다. topology가 진동에 과민 반응해 깨지지 않는다.
- 세부 동작: provider 진동 내성.

### Track B — in-flight와 shutdown

#### RL-B1 client 취소와 pending 정리

우선순위: `P1`

- 절차: 처리 중인 request를 client가 취소(또는 timeout)한다.
- 검증: client 측 pending submit이 취소되어 정리되고, 같은 client의 후속 request가 정상 동작한다. (server handler는 현재 channel 경로에 protocol cancel token이 없어 작업을 계속 완료할 수 있다 — server-side 취소 전파는 단언하지 않는다. 늦은 server 완료가 client를 오염시키지 않는 것까지만 본다.)
- 세부 동작: client-side 취소 + pending 정리(서버 전파 아님).

#### RL-B2 in-flight request 중 provider crash

우선순위: `P1`

- 절차: handler가 처리 중일 때 provider를 SIGKILL한다.
- 검증: 해당 request는 정해진 public error로 끝나고 pending이 남지 않는다. 다른 provider로의 트래픽은 영향 없음.
- 세부 동작: in-flight 실패 처리.

#### RL-B3 graceful shutdown

우선순위: `P1`

- 절차: provider에 정상 종료(`StopAsync`/lifetime stop)를 요청한다.
- 검증: 종료 후 provider가 registry topology에서 빠지고 consumer가 그 endpoint로 더 가지 않는다(stale 회피). 종료 시점에 이미 완료된 request의 reply는 정상 수신된다. (현재 host 종료는 runtime stop token을 먼저 취소하므로 "진행 중 request를 끝까지 drain"하는 public admin/drain 모드는 가정하지 않는다 — drain 모드가 추가되면 별도 검증.)
- 세부 동작: 정상 종료 시 topology 이탈 + stale 회피.

### Track C — 정리와 partition

#### RL-C1 resource cleanup

우선순위: `P1`

- 절차: 다량의 연결·요청 뒤 client/서버를 정상 종료한다.
- 검증: 소켓·핸들·메모리 등 리소스가 누수 없이 정리된다.
- 세부 동작: 리소스 정리.

#### RL-C2 registry stale data cleanup

우선순위: `P2`

- 절차: provider를 `kill(SIGKILL)`로 비정상 종료해 unregister를 못 한 상태를 만든다(하네스 kill 연산 필요).
- 검증: registry가 heartbeat timeout(TTL)으로 stale entry를 결국 제거하고(topology query에서 사라짐), consumer의 follow-up request가 살아 있는 provider로만 간다. (현재 scale-in 테스트는 graceful `StopAsync`만 다루므로, 이 crash+TTL 경로는 kill 하네스가 갖춰지기 전엔 미구현으로 둔다.)
- 세부 동작: 비정상 종료 + TTL stale 정리.

#### RL-C3 노드 단절(프로세스 정지) 후 복구

우선순위: `P2`

- 절차: provider 또는 registry 노드를 정지(SIGSTOP/SIGTERM)했다가 복구·재기동한다. (실제 network partition은 proxy/iptables 같은 별도 harness가 필요하며 현재 프로세스 제어 harness 범위 밖이다. 이 시나리오는 프로세스 정지/복구로 단절을 모사한다.)
- 검증: 단절 중엔 정해진 public error(timeout/route-not-connected 등), 복구 뒤 provider 재광고와 topology 재수렴으로 messaging이 정상화된다. split-brain snapshot이 남지 않는다.
- 세부 동작: 노드 정지/복구 모사 + 재수렴.

#### RL-C4 registry restart/outage 복구

우선순위: `P1`

- 절차: provider·consumer가 도는 중 registry 프로세스를 `restart`한다(하네스 restart 필요).
- 검증: registry 다운 동안 **이미 연결된 channel socket의 messaging은 계속 동작한다**(discovery는 registry에 의존하지만 수립된 연결 자체는 registry와 독립). 새 discovery·원격 topology query는 자동 재시도하지 않으므로 명시적 재조회로 확인한다. registry 복구 후 provider가 heartbeat로 재등록되고, consumer가 재조회 시 topology가 정상화되며 follow-up request가 성공한다.
- 세부 동작: 연결 socket은 registry와 독립 + 복구 후 heartbeat 재등록·재조회.

### Track D — 부하와 실패 중 관측

#### RL-D1 high fanout stability

우선순위: `P2`

- 절차: 많은 subscriber/consumer로 높은 fanout 부하를 준다.
- 검증: 누락·붕괴 없이 안정적으로 처리된다.
- 세부 동작: 고부하 안정성.

#### RL-D2 observer 실패 격리

우선순위: `P1`

- 절차: dispatch-error observer 자체가 예외를 던지게 한다.
- 검증: observer 실패가 messaging 경로를 막지 않는다(격리). observer 예외는 runtime error sink로 보고된다(일반 logger 로깅 계약 아님) — `.NET`은 `ZLinkRuntimeErrorSink.UnhandledCallbackException` event, Node는 `reportRuntimeTaskException('dispatch-error-observer', ...)`(`onRuntimeTaskException`)로 관측한다.
- 세부 동작: 관측 격리(언어별 error sink event).

#### RL-D3 로그 marker 관측

우선순위: `P1`

- 절차: 명시적 logging provider/sink를 단 server에서 오류·핵심 전이를 발생시킨다.
- 검증: 등록한 logging sink(또는 evidence endpoint)에 dispatch 오류 marker(reason·action·packetName)가 남는다. (이 config의 server는 logging을 clear하지 않고 marker를 수집할 sink를 명시적으로 단다 — 어떤 sink를 쓰는지 시나리오가 고정한다.)
- 세부 동작: 오류 marker 로깅(명시 sink).

#### RL-D4 error reply 직렬화(same-version)

우선순위: `P2`

- 절차: 같은 버전 provider/consumer 사이에서 다양한 public error를 발생시켜 error reply를 주고받는다.
- 검증: error reply는 wire header에 error code/message를 싣는다 — **raw wire 키는 Web JSON camelCase `errorCode`/`errorMessage`**, 디코드된 .NET 속성명은 `ErrorCode`/`ErrorMessage`다. normal client는 `errorMessage` 기반 예외만 던지고 code는 노출하지 않으므로, client-side는 message 예외만 단언한다. code round-trip은 raw envelope/header(키 `errorCode`) 검사 또는 server-side evidence로 확인한다. dispatch `Reason`/`Action`은 wire에 실리지 않으므로 server observer evidence에서 확인한다. (버전 간 호환은 old/new 빌드 아티팩트·버전 매트릭스가 필요해 별도 harness 도입 시 확장.)
- 세부 동작: error reply `ErrorCode`/`ErrorMessage` round-trip(동일 버전).

## 5. 완료 기준

- `P1` 시나리오는 지원 언어에서 구현한다. `P2`는 선택이며 미구현 이유를 남긴다.
- 복구 시나리오는 복구 후 follow-up request 성공 + topology query의 제거/추가 반영으로 관측한다(내부 pending/stale 상태는 public 표면이 아니므로 직접 단언하지 않는다).
- public contract만 직접 호출하고 `ensure`로 단언한다.
