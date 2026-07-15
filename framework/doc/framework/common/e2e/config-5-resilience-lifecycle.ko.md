<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 등록·codec](config-4-registration-codec.ko.md) | [다음: Store 장애·복구](config-6-store-failure-recovery.ko.md)
<!-- framework-adapter-nav:end -->

# Config 5 — Resilience·lifecycle 배포

다중 노드 + 공유 location store 배포를 띄운 뒤, **프로세스를 실제로 죽이고 다시 띄우며**
복구·수명·정리가 의도대로 도는지 본다. 비용이 큰 시나리오가 많아 대부분 `P1`·`P2`다.

## 1. 목적과 범위

- 다룬다: 프로세스 restart·replacement·provider failover, client reconnect, in-flight 중 crash,
  cancellation, graceful shutdown, resource·stale 정리, 노드 단절 복구, rolling·blue-green 전환,
  실패 중 관측.
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

**한마디로:** 같은 논리 provider가 중단되었다가 같은 rid·endpoint로 재시작하면, 다운 구간에는
정해진 오류가 발생하고 복구 뒤에는 consumer 재시작 없이 정상화되는가.

- 절차: 이 시나리오에서는 대체 provider를 두지 않는다. provider에 `SIGTERM`을 보내 정상 종료를
  시작하고 terminal `Drained`와 peer location 성공 조회에서 old row 제거를 확인한다. down 상태에서
  짧은 timeout의 target 미지정 request 한 건을 보낸 뒤 provider를 같은 rid·endpoint로 재시작한다.
- 검증: 종료 전에 시작한 request는 drain deadline 안에서 정상 reply로 끝난다. old row 제거 뒤의
  target 미지정 request는 send readiness 한계 안에서 `RouteNotConnected`로 끝나며 자동 재전송되지
  않는다. 재시작 뒤 peer location row가 같은 rid·endpoint와 새 owner generation으로 다시 조회되고
  `ConnectionReady` 뒤 follow-up request 20개가 모두 성공한다. consumer 재시작은 없다. crash restart는
  RL-A2의 old lease 만료 경로에서 별도로 검증한다.
- 세부 동작: 재기동 복구.

#### RL-A2 Kubernetes식 pod replacement

우선순위: `P2`

**한마디로:** provider가 다른 endpoint·같은 rid로 새로 떠도, peer location row가 새 주소로 갱신되고 consumer가 죽은 주소로 가지 않는가(in-flight·반복 복구 관점).

- 절차: 처리 시간을 제어할 수 있는 request가 old provider handler에서 시작됐다는 evidence를 확인한 뒤
  provider를 `SIGKILL`한다. old peer row가 owner lease 만료로 성공 조회에서 제외될 때까지 기다린 다음,
  다른 endpoint·같은 rid의 provider를 시작한다. (e2e server는 channel
  `SetRoutingId(...)`/`routingId(...)` 구성 옵션이 있어야 대체 provider가 같은 rid로 등록할 수 있다.)
- 검증: old in-flight request는 RL-B2의 crash 오류 계약대로 유한 시간 안에 끝난다. peer location
  row가 새 owner generation과 새 endpoint로 다시 조회되고, consumer가 old endpoint로 가지 않는다.
  replacement 뒤 반복 request가 모두 새 provider evidence에 기록된다. (정상 종료 replacement 경로는
  Config 1 RM-A4, 여기서는 crash in-flight·반복 복구 관점을 검증한다.)
- 세부 동작: 같은 논리 identity의 새 endpoint replacement. 이미 실행 중인 다른 provider가 처리를
  계속하는 failover와 구분한다.

#### RL-A3 client reconnect storm

우선순위: `P1`

**한마디로:** 100개 client 연결이 server 정상 재시작 뒤 함께 복구되어도 30초 상한 안에 모두
준비되고 messaging이 정상화되는가.

- 절차: client 100개를 시작해 각각 request 한 건의 성공과 연결 준비를 확인한다 → server에
  `SIGTERM`을 보내 terminal `Drained`와 old row 제거를 기다린다 → 같은 rid·endpoint로 server를
  재시작한다 → client별 `ConnectionReady`를 최대 30초까지 기다린다 → 각 client가 고유 correlation의
  request를 한 건씩 보낸다.
- 검증: 재시작 뒤 30초 안에 100개 client가 모두 `ConnectionReady`를 기록하고 100개 follow-up request가
  각각 한 번 성공한다. 중복 reply, 무한 reconnect loop, 남은 pending request가 없어야 한다. client
  application이 별도 reconnect loop를 만들면 실패다.
- 세부 동작: 대량 재연결 안정성.

#### RL-A4 rolling update / blue-green 전환

우선순위: `P2`

**한마디로:** serving target을 유지하며 provider를 한 대씩(rolling) 또는 set 단위(blue-green)로
교체해도 request가 중단되지 않고, 완료 뒤에는 새 version만 처리하는가.

- 절차: old와 new provider는 overlap 구간에 서로 다른 rid를 사용하고 version은 handler evidence에
  기록한다. request를 고정된 간격과 timeout으로 계속 보낸다. rolling 경로는 새 version provider 하나를
  새 rid로 시작해 peer row 반영과 실제 request 성공을 확인한 뒤 old provider 하나를 `Drain(deadline)`으로
  정상 종료하며, 같은 순서를 old provider가 없어질 때까지 반복한다. blue-green 경로는 green set 전체를
  시작해 모든 peer row와 각 provider의 request evidence를 확인한 뒤 blue set을 drain한다. 각 drain은
  `Draining=true` 반영과 terminal `Drained`를 확인하고, 다음 old provider를 내리기 전에 하나 이상의
  serving new provider가 남아 있는지 runtime query로 확인한다.
- 검증: 전환 구간의 모든 request가 설정한 timeout 안에 정상 reply로 끝나고 pending이 남지 않는다.
  각 단계에서 serving target이 0이 되는 순간이 없어야 한다. 완료 뒤 peer location 성공 조회에는 새
  version set의 rid만 남고, 검증 구간의 신규 request evidence도 새 version에서만 기록된다. version은
  peer row의 존재만으로 추정하지 않고 실제 handler evidence로 판정한다. old endpoint로의
  반복 timeout, old provider의 신규 handler-start evidence, `ForceStopping`은 없어야 한다.
- 세부 동작: 무중단 배포 전환.

#### RL-A5 provider flapping

우선순위: `P2`

**한마디로:** provider B를 정상 종료·재시작하는 cycle을 반복해도 A의 처리는 유지되고 B의 location과
연결이 매번 하나의 새 generation으로 수렴하는가.

- 절차: A·B 분산을 확인한다. 아래 cycle을 5회 반복한다: B에 `SIGTERM` → terminal `Drained`와 B row
  제거 확인 → target 미지정 request 10개가 모두 A에서 성공 → B를 같은 rid·endpoint로 재시작 → B의
  새 owner generation row와 `ConnectionReady` 확인 → 최대 20개 request 안에 A·B handler evidence를
  모두 확인. 다음 cycle은 이 준비가 끝난 뒤에만 시작한다.
- 검증: 5회 모두 B의 유효한 row는 최대 하나이고 generation은 직전 cycle보다 증가한다. B가 제외된
  구간의 request는 모두 A에서 성공하고, B 복구 뒤에는 A·B가 다시 처리한다. stale endpoint timeout,
  claim conflict, 중복된 유효 row와 pending request가 없어야 한다. crash·lease 만료 반복은 RL-C2가
  다룬다.
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
- 검증: 연결 종료가 먼저 관측되면 해당 request는 retriable `RouteNotConnected`, 이미 제출되어 handler
  실행 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의 timeout으로 끝난다. 오류는
  socket/location 상태 evidence와 함께 기록한다. framework가 in-flight request를 다른 provider로 자동
  재전송했다고 단언하지 않는다. 같은 consumer가 다른 provider로 보내는 follow-up request는 성공한다.
- 세부 동작: in-flight 실패 처리. crash 이후 이미 실행 중인 다른 provider가 신규 부하를 계속
  처리하는 failover 결과는 Config 1 RM-B3와 함께 검증한다.

#### RL-B3 graceful shutdown

우선순위: `P1`

**한마디로:** provider를 정상 종료하면 peer location list에서 빠지고 consumer가 그쪽으로 안 가며, 종료 직전 끝난 request의 reply는 정상 수신되는가.

- 절차: provider에 정상 종료(`StopAsync`/lifetime stop)를 요청한다.
- 검증: 종료 후 provider의 peer location row가 store에서 제거되고(runtime query peer list에서 사라짐 — shutdown 경로의 owner lease 제거 + row bulk remove) consumer가 그 endpoint로 더 가지 않는다(stale 회피). 종료 시점에 이미 완료된 request의 reply는 정상 수신된다. socket weight로 신규 부하만 제외하는 경로는 RL-B4·RL-B5에서, graceful drain lifecycle은 RL-A4와 Config 11에서 별도로 다룬다.
- 세부 동작: 정상 종료 시 location row 이탈 + stale 회피.

#### RL-B4 socket weight 부하 제외 / 복원

우선순위: `P0`

**한마디로:** 운영 중 provider의 socket weight를 0으로 바꾸면 신규 request의 부하 분산 대상에서
제외되고, 원래 값으로 복원하면 다시 대상에 포함되는가.

- 절차: provider 2대로 분산 중, 한 노드의 admin 경로에서
  `IZLinkChannelRuntimeOptions.ClientServerChannel(name).ConfigureServerSocket().Weight = 0`으로
  바꾼다. local getter와 peer row가 weight 0을 반영한 뒤 consumer가 request를 계속 보내 실제 전파를
  확인한다. 이후 같은 노드를 `Weight = 100`으로 복원하고 다시 실제 트래픽으로 반영을 확인한다.
- 검증: weight 0 전파를 확인한 뒤의 검증 구간에는 신규 request가 해당 노드 evidence에 기록되지 않고
  다른 노드가 처리한다(후보가 그 노드뿐이면 정해진 public error). 노드는 종료되지 않고 peer location
  row와 기존 연결도 유지된다. weight 복원 뒤 다시 부하 분산 대상이 되어 request를 처리한다. consumer
  재시작은 없다. local getter 변경만으로 전파 완료를 판정하지 않는다.
- 세부 동작: peer weight 기반 transport 부하 제외·복원(노드 종료와 graceful drain lifecycle 아님).

> **`Weight`와 `Draining` 마커는 다른 축이다.** `Weight = 0`은 **transport 수준 부하 게이트**다.
> `Draining` 마커는 drain handoff 대상과 remote user Spot actor join 대상에서 해당 노드를 제외하며,
> drain에 들어간 노드는 로컬 신규 admission을 차단한다. 반면 로컬 spot `GetOrCreate`와 호출자가
> 대상을 지정하는 Entry Spot join은 배치 후보 선택 경로가 아니므로 마커를 읽지 않는다
> ([54 §3.1](../../spec/server/54-graceful-drain-handoff.ko.md)). 이 시나리오는 channel request 부하 축만
> 검증한다. actor/spot drain 동작은 Config 11이 담당한다.

#### RL-B5 socket weight 변경 중 in-flight 완료

우선순위: `P0`

**한마디로:** socket weight를 0으로 바꾸기 전에 처리 중이던 request는 영향받지 않고 정상 reply로
끝나며, 전파 완료 뒤의 신규 request만 해당 provider의 부하 분산 대상에서 제외되는가.

- 절차: provider가 느린 handler(`value=="slow"`)로 request를 처리하고 있다는 handler-start evidence를
  확인한 뒤 그 provider의 `Weight = 0`으로 바꾼다. local getter와 peer row 반영 뒤 신규 request를
  계속 보내 실제 전파 완료를 확인한다.
- 검증: weight 변경 전에 시작한 request는 끝까지 완료되어 reply가 정상 수신된다. 전파 완료 뒤의 신규
  request는 해당 provider evidence에 기록되지 않고 다른 provider가 처리한다. 완료 후 pending이 남지
  않는다. 이 시나리오는 `Draining` 상태 전이, in-flight 대기, actor handoff를 단언하지 않는다.
- 세부 동작: socket weight 변경과 기존 in-flight request의 독립성.

#### RL-B6 부분 degradation (gray failure)

우선순위: `P1`

**한마디로:** provider 하나가 느려지거나 일부 request에 실패해도 다른 provider의 reply가 오염되지
않고 각 request가 해당 provider의 결과로 유한 시간 안에 끝나는가.

- 절차: provider A는 정상 응답하고 B는 correlation 끝자리가 짝수면 `RequestFailed` error reply, 홀수면
  request timeout보다 길게 응답을 지연하도록 결과가 입력으로 결정되는 fault를 켠다. 두 provider의
  weight를 같은 값으로 유지한 채 target 미지정 request 200개를 최대 동시 실행 수 20으로 보낸다.
  provider handler-start evidence로 각 request의 처리 provider를 식별한다. provider 상태에 따른 자동
  routing이나 circuit breaker를 가정하지 않는다.
- 검증: A와 B가 각각 20개 이상을 처리하고 B가 짝수·홀수 correlation을 각각 하나 이상 받아야 한다.
  A handler-start evidence가 있는 request는 모두 설정한 request timeout 안에 정상 reply로 끝난다. B
  evidence가 있는 request는 주입 규칙과 같은 `RequestFailed` 또는 timeout으로 각각 한 번 끝난다.
  pending이 남지 않고 B의 실패가 A의 payload와 reply correlation을 오염시키지 않는다. framework가 B를
  자동으로 제외하거나 A로 request를 재전송했다고 단언하지 않는다.
- 세부 동작: 고정 routing 조건의 gray failure 격리와 유한 완료.

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

#### RL-C3 프로세스 정상 정지·재시작 후 topology 재수렴

우선순위: `P2`

**한마디로:** provider process를 정상 종료했다가 재시작하면 old location이 제거되고 새 owner
generation으로 topology가 다시 수렴하며 같은 rid의 유효한 row가 둘 남지 않는가.

- 절차: provider process를 `SIGTERM`으로 정상 종료하고 peer location 성공 조회에서 old row가 제거됐는지
  확인한다. 그 뒤 같은 rid·endpoint로 process를 재시작하고 runtime query와 지속 request를 관찰한다.
  실제 network partition과 `SIGSTOP`/`SIGCONT` pause는 연결·lease 조건이 다르므로 이 시나리오에 섞지
  않는다. network partition은 proxy/iptables 같은 별도 harness가 마련되면 별도 시나리오로 추가한다.
- 검증: 종료 구간의 request 결과는 RL-A1의 restart 오류 계약을 따른다. 복구 뒤 provider가 새 owner
  generation의 owner lease와 peer row를 등록하고 peer topology가 다시 수렴해 messaging이 정상화된다.
  같은 rid의 유효한 peer row는 하나뿐이며 old generation row는 성공 결과에 남지 않는다.
- 세부 동작: 정상 process restart 뒤 owner generation·topology 재수렴.

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

**한마디로:** 고정한 장시간 기준 부하에서 처리량·latency·오류·pending 추이를 수집해 release 판단에
쓸 운영 자료를 남기는가.

- 절차: 기준 부하는 client 20개, 5분, client당 초당 request 5건과 send 5건으로 고정한다.
  1분에 provider scale-out, 2분에 기존 provider 하나의 weight 0, 3분에 weight 복원, 4분에 해당 provider
  정상 scale-in을 실행하며 각 단계의 readiness를 확인한다. 환경별 부하 값을 바꾸면 결과에 실제 값을
  함께 기록한다.
- 검증: 분 단위 처리량, request 성공·오류 수, p50/p95/p99 latency, 최대 pending과 종료 후 pending을
  기록한다. send 완료는 원격 수신 보장으로 계산하지 않는다. 이 항목은 환경 성능에 의존하는
  **비차단 운영 관측**이며 공통 contract PASS를 판정하지 않는다. 기능 정합성은 각 단계의 RM-B1,
  RM-B2, RL-B4, RL-B5와 RL-C1 결과로 별도 판정한다.
- 세부 동작: 재현 가능한 기준 부하의 비차단 soak 관측.

## 5. 완료 기준

- `P1` 시나리오는 지원 언어에서 구현한다. `P2`는 선택이며 미구현 이유를 남긴다.
- 복구 시나리오는 복구 후 follow-up request 성공 + runtime query peer location list의 제거/추가 반영으로 관측한다(내부 pending/stale 상태는 public 표면이 아니므로 직접 단언하지 않는다).
- public contract만 직접 호출하고 `ensure`로 단언한다.
