<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 등록·codec](config-4-registration-codec.ko.md) | [다음: Store 장애·복구](config-6-store-failure-recovery.ko.md)
<!-- framework-adapter-nav:end -->

# Config 5 — Resilience·lifecycle 배포

다중 노드 + 공유 location store 배포를 띄운 뒤, **프로세스를 실제로 죽이고 다시 띄우며**
복구·수명·정리가 의도대로 도는지 본다. 비용이 큰 시나리오가 많아 대부분 `P1`·`P2`다.

## 1. 목적과 범위

- 다룬다: 프로세스 restart·재스케줄, client reconnect, in-flight 중 crash, cancellation, graceful shutdown, resource·stale 정리, 노드 단절 복구, rolling·blue-green 전환, 실패 중 관측.
- 여기서 다루지 않는 것: 정상 경로 messaging/resolve(Config 1·2), codec(Config 4), store 자체의 장애·복구 매트릭스(Config 6 — 여기서는 RL-C4가 store 독립성만 가볍게 본다).

## 2. 서버 구성 (한 번 구동 + 동적 조작)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 전용 key prefix. RL-C4에서 일시 정지 대상. |
| provider | 2~3 | Config 1과 같은 channel provider(`AddLocationStore(...)`로 store 등록, peer row 자동 갱신). 시나리오가 죽이고/재시작/교체한다. |
| consumer | 시나리오별 | 지속 트래픽을 보내며 복구를 관측한다. |

스크립트가 기본 배포를 띄운 뒤, 시나리오별로 provider 프로세스(또는 RL-C4의 store 프로세스)를
SIGTERM·SIGKILL로 종료하거나 새 endpoint로 재기동한다.

## 3. 실행 모델

`run_e2e.sh`가 배포를 띄우고, 각 시나리오 client가 트래픽을 보내는 동안 스크립트가 프로세스를
조작한다.

**하네스 전제(필수 확장):** 이 config는 실행 중 프로세스 제어가 필요하다. 지금 ScenarioE2E
runner는 "시작 → cleanup → 종료"만 지원하므로, 아래 시나리오를 돌리려면 harness에 명시적
`stop(SIGTERM)`·`kill(SIGKILL)`·`restart`·(필요 시 `pause/resume`) 연산을 먼저 넣어야 한다.
이 연산이 없으면 해당 시나리오는 "미구현(하네스 대기)"로 둔다.

**성공 기준 어휘:** "정해진 public error"는 시나리오마다 정확한 `ZLinkFrameworkErrorKind`
(`RouteNotConnected`·`RequestTargetNotFound`·`RequestRejected`·`RequestFailed`) 또는
`TimeoutException`과, 그 retriable 여부·timeout window를 명시한다. 재시도가 framework 동작인지
client harness 동작인지도 구분한다. 복구는 "이후 follow-up request 성공 +
`IZLinkLocationRuntimeQuery.ListPeerLocationsAsync(filter)`의 peer location list에서 제거/추가 반영"처럼
**눈으로 확인 가능한 결과**로 판정한다(내부 pending dict는 public 표면이 아니므로 직접 단언하지
않는다).

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다. 특히
crash·drain·failover 시나리오는 `corr=` 흐름으로 어디서 끊겼는지 좁힌다.

## 4. 시나리오

### Track A — restart와 재연결

#### RL-A1 server restart

우선순위: `P1`

**한마디로:** provider가 죽었다 같은 자리로 살아나면, 다운 구간엔 정해진 에러가 나고 복구 뒤엔 (consumer 재시작 없이) 정상화되는가.

- 절차: provider를 종료했다가 같은 endpoint로 재시작한다. consumer는 계속 request를 보낸다.
- 검증: 다운 구간엔 정해진 public error/재시도, 복구 뒤 request 정상화. consumer 재시작 없음.
- 세부 동작: 재기동 복구.

#### RL-A2 Kubernetes식 pod 재스케줄

우선순위: `P2`

**한마디로:** provider가 다른 endpoint·같은 rid로 새로 떠도, peer location row가 새 주소로 갱신되고 consumer가 죽은 주소로 가지 않는가(in-flight·반복 복구 관점).

- 절차: provider를 죽이고 다른 endpoint·같은 rid로 새로 띄운다. (e2e server는 channel `SetRoutingId(...)`/`routingId(...)` 구성 옵션이 있어야 대체 provider가 같은 rid로 등록할 수 있다.)
- 검증: peer location row가 그 rid의 endpoint를 새 값으로 갱신하고(runtime query peer list로 확인), consumer가 stale로 가지 않는다. (정상 경로는 Config 1 RM-A4, 여기선 in-flight·반복 복구 관점)
- 세부 동작: 재스케줄 복구.

#### RL-A3 client reconnect storm

우선순위: `P1`

**한마디로:** 수많은 client가 동시에 끊겼다가 한꺼번에 몰려 재접속해도, server가 안정적으로 받아내고 messaging이 정상화되는가.

- 절차: 다수 client가 동시에 끊겼다가 한꺼번에 재접속한다.
- 검증: 재접속 폭주에도 server가 안정적으로 수용하고 messaging이 정상화된다.
- 세부 동작: 대량 재연결 안정성.

#### RL-A4 rolling update / blue-green 전환

우선순위: `P2`

**한마디로:** provider를 한 대씩(rolling) 또는 통째로(blue-green) 새 버전으로 교체해도 무중단으로 처리되고, 전환 뒤엔 신규 set으로만 가는가.

- 절차: provider를 한 대씩 새 버전으로 교체(rolling)하거나, 새 set으로 일괄 전환(blue-green)한다.
- 검증: 전환 중에도 무중단으로 request가 처리되고, 전환 완료 뒤 신규 set으로만 routing된다.
- 세부 동작: 무중단 배포 전환.

#### RL-A5 provider flapping

우선순위: `P2`

**한마디로:** provider 하나가 짧은 간격으로 떴다 죽었다 반복해도, consumer가 살아 있는 쪽으로 안정적으로 수렴하고 peer location 반영이 진동에 깨지지 않는가.

- 절차: provider 하나를 짧은 간격으로 down/up 반복(flapping)시키며 consumer가 request를 계속 보낸다.
- 검증: flapping 중에도 consumer가 살아 있는 provider로 안정적으로 수렴하고, stale endpoint로 반복 timeout 하지 않는다. peer location 반영과 reconcile이 진동에 과민 반응해 깨지지 않는다.
- 세부 동작: provider 진동 내성.

### Track B — in-flight와 shutdown

#### RL-B1 client 취소와 pending 정리

우선순위: `P1`

**한마디로:** 처리 중인 request를 client가 취소하면 그쪽 pending이 깔끔히 정리되고, 뒤늦은 server 완료가 다음 요청을 오염시키지 않는가.

- 절차: 처리 중인 request를 client가 취소(또는 timeout)한다.
- 검증: client 측 pending submit이 취소되어 정리되고, 같은 client의 후속 request가 정상 동작한다. (server handler는 현재 channel 경로에 protocol cancel token이 없어 작업을 계속 완료할 수 있다 — server-side 취소 전파는 단언하지 않는다. 늦은 server 완료가 client를 오염시키지 않는 것까지만 본다.)
- 세부 동작: client-side 취소 + pending 정리(서버 전파 아님).

#### RL-B2 in-flight request 중 provider crash

우선순위: `P1`

**한마디로:** 처리 도중 provider를 강제로 죽였을 때, 그 request는 정해진 에러로 끝나고 pending이 안 남으며 다른 provider 트래픽은 멀쩡한가.

- 절차: handler가 처리 중일 때 provider를 SIGKILL한다.
- 검증: 해당 request는 정해진 public error로 끝나고 pending이 남지 않는다. 다른 provider로의 트래픽은 영향 없음.
- 세부 동작: in-flight 실패 처리.

#### RL-B3 graceful shutdown

우선순위: `P1`

**한마디로:** provider를 정상 종료하면 peer location list에서 빠지고 consumer가 그쪽으로 안 가며, 종료 직전 끝난 request의 reply는 정상 수신되는가.

- 절차: provider에 정상 종료(`StopAsync`/lifetime stop)를 요청한다.
- 검증: 종료 후 provider의 peer location row가 store에서 제거되고(runtime query peer list에서 사라짐 — shutdown 경로의 owner lease 제거 + row bulk remove) consumer가 그 endpoint로 더 가지 않는다(stale 회피). 종료 시점에 이미 완료된 request의 reply는 정상 수신된다. (host 종료가 아니라 "진행 중 request를 끝까지 drain"하는 런타임 drain 모드는 RL-B4·RL-B5에서 별도로 다룬다.)
- 세부 동작: 정상 종료 시 location row 이탈 + stale 회피.

#### RL-B4 런타임 drain / restore (무중단 배포)

우선순위: `P0`

**한마디로:** 운영 중 provider를 런타임에 drain하면 새 request가 그 노드로 안 가고, restore하면 다시 받는가(노드를 죽이거나 location row를 지우지 않고).

- 절차: provider 2대로 분산 중, 한 노드의 admin 경로에서 `IZLinkChannelRuntimeOptions.ClientServerChannel(name).ConfigureServerSocket().Weight = 0`으로 drain한다. consumer는 계속 request를 보낸다. 잠시 뒤 같은 노드를 `Weight = 100`으로 restore한다.
- 검증: drain 후 신규 request는 그 노드 evidence에 더 기록되지 않고 살아 있는 다른 노드가 받는다(후보가 그 노드뿐이면 정해진 public error). 노드는 죽지 않고 peer location row도 store에 남아 있다(runtime query peer list로 확인). restore 후 다시 routing 대상이 되어 request를 받는다. consumer 재시작 없음.
- 세부 동작: peer weight 기반 런타임 graceful drain·restore(노드/소켓 종료 아님).

> **`Weight`와 `Draining` 마커는 다른 축이다.** `Weight = 0`은 **transport 수준 부하 게이트**로,
> client가 그 노드로 새 request를 보내지 않게 한다. framework의 **배치 결정**(actor join target,
> spot `GetOrCreate` 노드 선택, Entry Spot 배정)은 weight를 보지 않고 `Draining` 마커만 본다
> ([54 §3.1](../spec/54-graceful-drain-handoff.ko.md)). 이 시나리오는 channel request 부하 축만
>검증한다 — actor/spot 배치 제외는 config 11의 drain 시나리오가 담당한다.

#### RL-B5 drain 중 in-flight 완료

우선순위: `P0`

**한마디로:** drain은 "새 요청만 차단"이라, drain 직전 도착해 처리 중이던 request는 끝까지 처리되고 reply가 정상으로 돌아오는가.

- 절차: provider가 느린 handler(`value=="slow"`)로 request를 처리하는 도중 그 provider를 `Weight = 0`으로 drain한다. drain 직후 새 request도 보낸다.
- 검증: drain 시점에 이미 처리 중이던 request는 끝까지 완료되어 reply가 정상 수신된다(drain이 진행 중 작업을 취소하지 않음). drain 이후의 신규 request만 그 노드로 가지 않는다. 완료 후 pending이 남지 않는다.
- 세부 동작: drain = 새 수신 차단, in-flight·reply는 유지.

#### RL-B6 부분 degradation (gray failure)

우선순위: `P1`

**한마디로:** provider가 죽는 게 아니라 "느려지거나 일부만 실패"할 때, 건강한 provider로 트래픽이 수렴하고 전체 성공률이 무너지지 않는가.

- 절차: provider 2대 중 하나가 (a) 일부 request에 간헐적으로 error reply를 내거나, (b) 응답이 느려 짧은 timeout을 유발하도록 fault를 주입한다(harness fault-injection 또는 server 옵션 필요 — 없으면 "미구현(하네스 대기)"). consumer는 지속 request를 보낸다.
- 검증: 느리거나 실패하는 provider가 섞여 있어도 건강한 provider로 충분한 트래픽이 처리되어 전체 성공률이 유지된다. client는 실패/timeout을 정해진 public error로 받고, 그 노드로 반복 timeout만 하지 않는다(가능하면 건강한 쪽 수렴). 정상 provider의 reply는 오염되지 않는다.
- 세부 동작: gray failure 내성(부분 에러·부분 지연 시 건강 노드 수렴).

### Track C — 정리와 partition

#### RL-C1 resource cleanup

우선순위: `P1`

**한마디로:** 연결·요청을 잔뜩 쓴 뒤 정상 종료하면, 소켓·핸들·메모리가 누수 없이 정리되는가.

- 절차: 다량의 연결·요청 뒤 client/서버를 정상 종료한다.
- 검증: 소켓·핸들·메모리 등 리소스가 누수 없이 정리된다.
- 세부 동작: 리소스 정리.

#### RL-C2 location store stale row cleanup

우선순위: `P2`

**한마디로:** provider가 row를 지우지 못하고 강제로 죽었을 때, owner lease 만료로 결국 그 row가 성공 결과에서 빠지고 consumer가 살아 있는 곳으로만 가는가.

- 절차: provider를 `kill(SIGKILL)`로 비정상 종료해 row remove를 못 한 상태를 만든다(하네스 kill 연산 필요).
- 검증: owner lease TTL 만료로 그 provider의 peer row가 resolve/list 성공 결과에서 결국 제외되고(runtime query peer list에서 사라짐 — 물리 삭제는 background cleanup 책임), consumer의 follow-up request가 살아 있는 provider로만 간다. (현재 scale-in 테스트는 graceful `StopAsync`만 다루므로, 이 crash+lease 만료 경로는 kill 하네스가 갖춰지기 전엔 미구현으로 둔다. lease 만료 자체의 정밀 검증은 Config 6 SF-C1이 담당한다.)
- 세부 동작: 비정상 종료 + owner lease 만료 stale 정리.

#### RL-C3 노드 단절(프로세스 정지) 후 복구

우선순위: `P2`

**한마디로:** 노드가 잠시 단절됐다 복구되면, 단절 중엔 정해진 에러가 나고 복구 뒤엔 재광고·재수렴으로 정상화되며 split-brain이 안 남는가.

- 절차: provider 노드를 정지(SIGSTOP/SIGTERM)했다가 복구·재기동한다. (실제 network partition은 proxy/iptables 같은 별도 harness가 필요하며 현재 프로세스 제어 harness 범위 밖이다. 이 시나리오는 프로세스 정지/복구로 단절을 모사한다. store 프로세스 정지는 Config 6이 다룬다.)
- 검증: 단절 중엔 정해진 public error(timeout/route-not-connected 등), 복구 뒤 provider의 owner lease와 peer row 재등록·peer location 재수렴으로 messaging이 정상화된다. stale row가 성공 결과에 남지 않는다.
- 세부 동작: 노드 정지/복구 모사 + 재수렴.

#### RL-C4 location store restart/outage 복구

우선순위: `P1`

**한마디로:** 공유 location store가 잠깐 죽어도 이미 연결된 채널은 계속 동작하고(fail-static), store가 살아나면 재등록·재조회로 peer location이 정상화되는가.

- 절차: provider·consumer가 도는 중 store(Redis) 프로세스를 `restart`한다(하네스 restart 필요).
- 검증: store 다운 동안 **이미 연결된 channel socket의 messaging은 계속 동작한다**(위치 resolve는 store에 의존하지만 수립된 연결 자체는 store와 독립 — fail-static). store 다운 중 read 표면은 store 장애를 not found가 아니라 infrastructure error로 구분해 돌려준다. store 복구 후 각 노드가 owner lease와 peer row를 다시 upsert하고, runtime query peer list가 정상화되며 follow-up request가 성공한다. (재등록 순서·heartbeat 유예·grace 초과 같은 장애 매트릭스 정밀 검증은 Config 6가 담당한다 — 여기서는 "수립된 연결의 store 독립 + 복구 후 정상화"만 본다.)
- 세부 동작: 연결 socket은 store와 독립(fail-static) + 복구 후 owner lease/row 재등록·재조회.

### Track D — 부하와 실패 중 관측

#### RL-D1 high fanout stability

우선순위: `P2`

**한마디로:** subscriber/consumer가 많은 고fanout 부하에서도 누락·붕괴 없이 안정적으로 처리되는가.

- 절차: 많은 subscriber/consumer로 높은 fanout 부하를 준다.
- 검증: 누락·붕괴 없이 안정적으로 처리된다.
- 세부 동작: 고부하 안정성.

#### RL-D2 observer 실패 격리

우선순위: `P1`

**한마디로:** dispatch-error observer가 예외를 던져도 messaging 경로는 막히지 않고, 그 예외는 runtime error sink로 보고되는가.

- 절차: dispatch-error observer 자체가 예외를 던지게 한다.
- 검증: observer 실패가 messaging 경로를 막지 않는다(격리). observer 예외는 runtime error sink로 보고된다(일반 logger 로깅 계약 아님) — `.NET`은 `ZLinkRuntimeErrorSink.UnhandledCallbackException` event, Node는 `reportRuntimeTaskException('dispatch-error-observer', ...)`(`onRuntimeTaskException`)로 관측한다.
- 세부 동작: 관측 격리(언어별 error sink event).

#### RL-D3 로그 marker 관측

우선순위: `P1`

**한마디로:** 명시적 logging sink를 단 server에서 오류가 나면, 그 sink에 dispatch 오류 marker(reason·action·packetName)가 남는가.

- 절차: 명시적 logging provider/sink를 단 server에서 오류·핵심 전이를 발생시킨다.
- 검증: 등록한 logging sink(또는 evidence endpoint)에 dispatch 오류 marker(reason·action·packetName)가 남는다. (이 config의 server는 logging을 clear하지 않고 marker를 수집할 sink를 명시적으로 단다 — 어떤 sink를 쓰는지 시나리오가 고정한다.)
- 세부 동작: 오류 marker 로깅(명시 sink).

#### RL-D4 error reply 직렬화(same-version)

우선순위: `P2`

**한마디로:** 같은 버전끼리 주고받는 `Error(5)` reply에 error code/message가 wire에 제대로 실려 왕복하는가(client는 message만, code는 raw/evidence로 확인).

- 절차: 같은 버전 provider/consumer 사이에서 다양한 public error를 발생시켜 error reply를 주고받는다.
- 검증: 실패 reply의 wire `message-kind`는 `Error=5`이고 header에 error code/message를 싣는다. 성공 reply는 `Response=2`이며 오류 필드를 싣지 않는다. 별도 `status` 필드는 없다. **raw wire 키는 Web JSON camelCase `errorCode`/`errorMessage`**, 디코드된 .NET 속성명은 `ErrorCode`/`ErrorMessage`다. normal client는 `errorMessage` 기반 예외만 던지고 code는 노출하지 않으므로, client-side는 message 예외만 단언한다. code round-trip은 raw envelope/header(키 `errorCode`) 검사 또는 server-side evidence로 확인한다. dispatch `Reason`/`Action`은 wire에 실리지 않으므로 server observer evidence에서 확인한다. 이전 event/status envelope를 받아들이는 호환 decoder는 검증 대상이 아니다.
- 세부 동작: `Response=2`/`Error=5` 분리와 `ErrorCode`/`ErrorMessage` round-trip(동일 버전).

#### RL-D5 지속 혼합 워크로드 soak

우선순위: `P2`

**한마디로:** 동시 다수 client가 request·send를 섞어 수 분간 계속 밀어 넣어도, 누락·붕괴·latency 악화·리소스 누수 없이 안정적으로 버티는가.

- 절차: 동시 N client가 request와 send를 섞어 지속(예: 수 분) 보낸다(지속 부하 harness 필요 — 없으면 "미구현(하네스 대기)"). 그 사이 provider scale-out/in이나 가벼운 drain을 섞을 수도 있다.
- 검증: 전 구간에서 붕괴 없이 처리되고 정상 reply 비율이 유지되는지, latency가 시간에 따라 단조 악화(drift)하지 않는지를 **관측으로 본다**(엄밀한 무손실·무누수 보장이 아니라 안정성 관측). 종료 후 pending이 남지 않고 리소스가 정리되는지는 RL-C1과 연계해 확인한다.
- 세부 동작: 지속 혼합 부하 안정성 관측(단건 burst가 아닌 soak).

## 5. 완료 기준

- `P1` 시나리오는 지원 언어에서 구현한다. `P2`는 선택이며 미구현 이유를 남긴다.
- 복구 시나리오는 복구 후 follow-up request 성공 + runtime query peer location list의 제거/추가 반영으로 관측한다(내부 pending/stale 상태는 public 표면이 아니므로 직접 단언하지 않는다).
- public contract만 직접 호출하고 `ensure`로 단언한다.
