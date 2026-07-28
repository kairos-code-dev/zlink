<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 등록·codec](config-4-registration-codec.ko.md) | [다음: Store 장애·복구](config-6-store-failure-recovery.ko.md)
<!-- framework-adapter-nav:end -->

# Config 5 — Resilience·lifecycle 배포

다중 노드와 공유 location store 배포를 시작한 뒤 프로세스를 강제 종료하거나 재시작해
복구·수명·정리가 의도대로 동작하는지 확인한다. 비용이 큰 시나리오가 많아 대부분 `P1`·`P2`다.

## 1. 목적과 범위

- 다룬다: 프로세스 restart·replacement·provider failover, client reconnect, in-flight 중 crash,
  cancellation, graceful shutdown, resource·stale 정리, 노드 단절 복구, rolling·blue-green 전환,
  실패 중 관측.
- 여기서 다루지 않는 것: 정상 경로 messaging/resolve(Config 1·2), codec(Config 4), store 자체의 장애·복구 매트릭스(Config 6 — 여기서는 RL-C4가 store 독립성만 가볍게 본다).

## 2. 서버 구성 (한 번 구동 + 동적 조작)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 전용 key prefix. RL-C4에서 일시 정지 대상. |
| relocation store | 1 | 공식 Redis relocation store extension이 사용하는 Redis instance. Location Store와 별도 key prefix와 등록 표면을 사용한다. Track F의 `Recreate`·`Snapshot` relocation payload와 accepted journal을 저장한다. |
| provider | 2~3 | Config 1과 같은 ChannelName provider다. `AddLocationStore(...)`로 store를 등록하면 MeshNode descriptor가 자동 갱신된다. Automatic discovery lifecycle마다 Framework가 새 RID를 발급한다. 시나리오에서 provider를 강제 종료·재시작·교체한다. |
| Object Server | 2 | Location Store와 Relocation Store를 등록한다. 두 node 모두 전체 participant inventory에 필요한 Actor 전체, Spot 전체와 Spot stable-type population capacity를 게시하고 아래 stable type의 factory, 명시적 relocation policy와 필요한 adapter를 같은 capability로 등록한다. |
| consumer | 시나리오별 | 지속 트래픽을 보내며 복구를 관측한다. |

Track F의 Object Server는 standalone Actor, User Spot, Instance Spot을 각각 검증할 stable type을 등록한다.
`Recreate` type은 factory와 policy만 등록하고, `Snapshot` type은 factory·policy와 Actor 또는 Spot
RelocationAdapter를 함께 등록한다. Entry membership 시나리오는 `Snapshot` Actor type을 사용한다. User Spot
aggregate 시나리오는 Spot과 member Actor를 모두 `Snapshot`으로 등록한다. Blocker를 검증할 때만 별도의
`Disabled` participant type을 사용한다. Target node의 capability와 capacity는 성공 시나리오의 participant
전체를 수용할 수 있어야 한다.

스크립트가 기본 배포를 시작한 뒤, 시나리오별로 provider 프로세스(또는 RL-C4의 store 프로세스)를
SIGTERM·SIGKILL로 종료하거나 새 endpoint로 재기동한다.

## 3. 실행 모델

`run_e2e.sh`가 배포를 시작하고, 각 시나리오 client가 트래픽을 보내는 동안 스크립트가 프로세스를
조작한다.

**하네스 전제(필수 확장):** 이 config는 실행 중 프로세스 제어가 필요하다. 지금 ScenarioE2E
runner는 "시작 → cleanup → 종료"만 지원하므로, 아래 시나리오를 돌리려면 harness에 명시적
`stop(SIGTERM)`·`kill(SIGKILL)`·`restart`·(필요 시 `pause/resume`) 연산을 먼저 넣어야 한다.
이 연산이 없으면 해당 시나리오는 "미구현(하네스 대기)"로 둔다.

**성공 기준 어휘:** "정해진 public error"는 시나리오마다 정확한 `ZLinkFrameworkErrorKind`
(`Unavailable`·`NotFound`·`Rejected`·`InternalFailure`) 또는
`TimeoutException`과, 그 retriable 여부·timeout window를 명시한다. 재시도가 framework 동작인지
client harness 동작인지도 구분한다. 복구는 "이후 follow-up request 성공과 public RouteMesh status의
ready peer·Channel target에서 제거·추가가 반영되는지 확인하는 방식처럼
**눈으로 확인 가능한 결과**로 판정한다(내부 pending dict는 public 표면이 아니므로 직접 단언하지
않는다).

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다. 특히
crash·termination·failover 시나리오는 `corr=` 흐름으로 어디서 끊겼는지 좁힌다.

## 4. 시나리오

### Track A — restart와 재연결

#### RL-A1 server restart

우선순위: `P1`

**검증 질문:** provider가 중단되었다가 같은 endpoint와 새 RID로 재시작하면, 다운 구간에는
정해진 오류가 발생하고 복구 뒤에는 consumer 재시작 없이 정상화되는가.

- 절차: 이 시나리오에서는 대체 provider를 두지 않는다. provider에 `SIGTERM`을 보내 정상 종료를
  시작하고 terminal `Stopped/None`과 store descriptor snapshot에서 old descriptor 제거를 확인한다. down 상태에서
  짧은 timeout의 target 미지정 request 한 건을 보낸 뒤 provider를 같은 endpoint로 재시작한다. Automatic
  discovery가 새 lifecycle RID를 발급하며 application은 이전 RID를 다시 설정하지 않는다.
- 검증: 종료 전에 시작한 request는 `Shutdown` deadline 안에서 정상 reply로 끝난다. old descriptor 제거 뒤의
  target 미지정 request는 send readiness 한계 안에서 `Unavailable`로 끝나며 자동 재전송되지
  않는다. 재시작 뒤 이전 RID와 다른 RID, 같은 endpoint를 가진 새 MeshNode descriptor가 조회되고
  `ConnectionReady` 뒤 follow-up request 20개가 모두 성공한다. consumer 재시작은 없다. crash restart는
  RL-A2의 old lease 만료 경로에서 별도로 검증한다.
- 세부 동작: 재기동 복구.

#### RL-A2 Kubernetes식 pod replacement

우선순위: `P2`

**검증 질문:** provider가 다른 endpoint와 새 RID로 replacement되어도 MeshNode descriptor가 새 주소로
갱신되고 consumer가 이전 주소를 선택하지 않는가.

- 절차: 처리 시간을 제어할 수 있는 request가 old provider handler에서 시작됐다는 evidence를 확인한 뒤
  provider를 `SIGKILL`한다. old MeshNode descriptor가 owner lease 만료로 성공 조회에서 제외될 때까지
  기다린 다음, 다른 endpoint에서 provider replacement를 시작한다. Automatic discovery가 이전 lifecycle과
  다른 새 RID를 발급하며 fixed RID 설정은 사용하지 않는다.
- 검증: old in-flight request는 RL-B2의 crash 오류 계약대로 유한 시간 안에 끝난다. MeshNode
  descriptor가 이전과 다른 RID, 새 owner generation과 새 endpoint로 다시 조회되고, consumer가 old endpoint를 선택하지 않는다.
  replacement 뒤 반복 request가 모두 새 provider evidence에 기록된다. (정상 종료 replacement 경로는
  Config 1 RM-A4, 여기서는 crash in-flight·반복 복구 관점을 검증한다.)
- 세부 동작: 새 lifecycle identity와 새 endpoint를 사용하는 replacement. 이미 실행 중인 다른 provider가 처리를
  계속하는 failover와 구분한다.

#### RL-A3 client reconnect storm

우선순위: `P1`

**검증 질문:** 100개 client 연결이 server 정상 재시작 뒤 함께 복구되어도 30초 상한 안에 모두
준비되고 messaging이 정상화되는가.

- 절차: client 100개를 시작해 각각 request 한 건의 성공과 연결 준비를 확인한다 → server에
  `SIGTERM`을 보내 terminal `Stopped/None`과 old descriptor 제거를 기다린다 → 같은 endpoint에서 새 RID로
  server를 재시작한다 → client별 `ConnectionReady`를 최대 30초까지 기다린다 → 각 client가 고유 correlation의
  request를 한 건씩 보낸다.
- 검증: 재시작 뒤 30초 안에 100개 client가 모두 `ConnectionReady`를 기록하고 100개 follow-up request가
  각각 한 번 성공한다. 중복 reply, 무한 reconnect loop, 남은 pending request가 없어야 한다. client
  application이 별도 reconnect loop를 만들면 실패다.
- 세부 동작: 대량 재연결 안정성.

#### RL-A4 rolling update / blue-green 전환

우선순위: `P2`

**검증 질문:** serving target을 유지하며 provider를 한 대씩(rolling) 또는 set 단위(blue-green)로
교체해도 request가 중단되지 않고, 완료 뒤에는 새 version만 처리하는가.

- 절차: old와 new provider는 overlap 구간에 서로 다른 rid를 사용하고 version은 handler evidence에
  기록한다. request를 고정된 간격과 timeout으로 계속 보낸다. 첫 반복에서는 green descriptor를 게시한 뒤
  handshake admission을 bounded gate에서 막고 old provider에 `Mode=RollingUpdate`, exact new
  `TargetApplicationVersion`과 deadline으로 `Relocate`를 시작한다. Source Core peer
  table에 green RID와 lifecycle generation이 아직 `Ready`가 아닌 동안 old host가 `Serving`과 application
  admission을 유지하는지 확인한 뒤 gate를 해제한다. Rolling 경로는 새 version provider 하나를 새 RID로
  시작해 exact peer `Ready`와 실제 request 성공을 확인한 뒤 old provider 하나를 같은 rolling update
  option으로 `Relocate`하고 `Relocated` 결과 뒤 `Shutdown`으로 종료하며, 같은 순서를 old provider가
  없어질 때까지 반복한다. Blue-green 경로는 green set 전체를
  시작해 모든 MeshNode descriptor와 각 provider의 request evidence를 확인한 뒤 blue set의 host를 하나씩
  같은 rolling update option으로 `Relocate`한다. 각 `Relocate`는 exact new version의 `Relocated/None`을
  확인한 뒤 `Shutdown`을 호출해 terminal `Stopped/None`을 확인하고, 다음 old provider를 내리기 전에 하나 이상의
  serving new provider가 남아 있는지 runtime query로 확인한다.
- 검증: 전환 구간의 모든 request가 설정한 timeout 안에 정상 reply로 끝나고 pending이 남지 않는다.
  Green descriptor와 connect intent만으로 old host가 `Relocating`으로 전환하면 실패다. Source가 exact green
  RID·lifecycle generation의 `Ready`를 확인한 뒤 old `Relocating`, relocation, old `Draining`과 accepted
  barrier, descriptor·owner lease release, old connection disconnect 순서가 유지되어야 한다.
  각 단계에서 serving target이 0이 되는 순간이 없어야 한다. 완료 뒤 descriptor 성공 조회에는 새
  version set의 RID만 남고, 검증 구간의 신규 request evidence도 새 version에서만 기록된다. version은
  MeshNode descriptor의 존재만으로 추정하지 않고 실제 handler evidence로 판정한다. old endpoint로의
  반복 timeout, old provider의 신규 handler-start evidence, `ForceStopped` outcome은 없어야 한다.
- 세부 동작: automatic topology의 connect-new, relocate-old, disconnect-old 순서를 지키는 무중단 배포 전환.

#### RL-A5 provider flapping

우선순위: `P2`

**검증 질문:** provider B를 정상 종료·재시작하는 cycle을 반복해도 A의 처리는 유지되고 B의 location과
연결이 매번 하나의 새 lifecycle RID로 수렴하는가.

- 절차: A·B 분산을 확인한다. 아래 cycle을 5회 반복한다: B에 `SIGTERM` → terminal `Stopped/None`과 B descriptor
  제거 확인 → target 미지정 request 10개가 모두 A에서 성공 → B를 같은 endpoint와 새 RID로 재시작 → B의
  새 descriptor와 `ConnectionReady` 확인 → 최대 20개 request 안에 A·B handler evidence를
  모두 확인. 다음 cycle은 이 준비가 끝난 뒤에만 시작한다.
- 검증: 5회 모두 종료한 B의 이전 RID는 성공 조회에서 제외되고 새 lifecycle RID의 유효한 descriptor만
  하나다. B가 제외된
  구간의 request는 모두 A에서 성공하고, B 복구 뒤에는 A·B가 다시 처리한다. stale endpoint timeout,
  claim conflict, 중복된 유효 descriptor와 pending request가 없어야 한다. crash·lease 만료 반복은 RL-C2가
  다룬다.
- 세부 동작: provider 진동 내성.

### Track B — in-flight와 shutdown

#### RL-B1 client 취소와 pending 정리

우선순위: `P1`

**검증 질문:** 처리 중인 request를 client가 취소하면 그쪽 pending이 깔끔히 정리되고, 뒤늦은 server 완료가 다음 요청을 오염시키지 않는가.

- 절차: 처리 중인 request를 client가 취소(또는 timeout)한다.
- 검증: client 측 pending submit이 취소되어 정리되고, 같은 client의 후속 request가 정상 동작한다. (server handler는 현재 channel 경로에 protocol cancel token이 없어 작업을 계속 완료할 수 있다 — server-side 취소 전파는 단언하지 않는다. 늦은 server 완료가 client를 오염시키지 않는 것까지만 본다.)
- 세부 동작: client-side 취소 + pending 정리(서버 전파 아님).

#### RL-B2 in-flight request 중 provider crash

우선순위: `P1`

**검증 질문:** 처리 도중 provider를 강제 종료했을 때 해당 request가 정해진 error로 끝나고 pending이
남지 않으며 다른 provider의 트래픽은 계속 처리되는가.

- 절차: handler가 처리 중일 때 provider를 SIGKILL한다.
- 검증: 연결 종료가 먼저 관측되면 해당 request는 `Unavailable`과 `RetryAfterBackoff`, 이미 제출되어 handler
  실행 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의 timeout으로 끝난다. 오류는
  socket/location 상태 evidence와 함께 기록한다. framework가 in-flight request를 다른 provider로 자동
  재전송했다고 단언하지 않는다. 같은 consumer가 다른 provider로 보내는 follow-up request는 성공한다.
- 세부 동작: in-flight 실패 처리. crash 이후 이미 실행 중인 다른 provider가 신규 부하를 계속
  처리하는 failover 결과는 Config 1 RM-B3와 함께 검증한다.

#### RL-B3 graceful shutdown

우선순위: `P1`

**검증 질문:** Provider를 정상 종료하면 ready peer·target에서 제외되고 consumer가 해당 connection을
선택하지 않으며, 종료 직전에 완료된 request의 reply는 정상 수신되는가.

- 절차: provider에 정상 종료(`StopAsync`/lifetime stop)를 요청한다.
- 검증: 종료 후 provider의 Node RID가 public RouteMesh status의 ready peer·Channel target에서 사라지고
  consumer가 이전 connection으로 더 가지 않는다. 종료 시점에 이미 완료된 request의 reply는 정상
  수신된다. Socket weight로 신규 부하만 제외하는 경로는 RL-B4·RL-B5에서, host `Relocate` lifecycle은
  RL-A4와 Config 11에서 별도로 다룬다.
- 세부 동작: 정상 종료 뒤 public topology 수렴과 stale connection 회피.

#### RL-B4 ChannelName weight 부하 제외 / 복원

우선순위: `P0`

**검증 질문:** 운영 중 provider의 ChannelName weight를 0으로 바꾸면 신규 request의 부하 분산 대상에서
제외되고, 원래 값으로 복원하면 다시 대상에 포함되는가.

- 절차: provider 2대로 분산 중 한 노드의 admin 경로에서
  `IZLinkRouteMeshRuntimeOptions.Channel(channelName).Weight = 0`으로 바꾼다.
  Local getter가 weight 0을 반영한 뒤 consumer가 request를 보내 실제 전파를 확인한다. 이후 같은
  노드를 `Weight = 100`으로 복원하고 다시 실제 트래픽으로 반영을 확인한다.
- 검증: weight 0 전파를 확인한 뒤의 검증 구간에는 신규 request가 해당 노드 evidence에 기록되지 않고
  다른 노드가 처리한다(후보가 그 노드뿐이면 정해진 public error). 노드는 종료되지 않고 descriptor와
  기존 연결도 유지된다. weight 복원 뒤 다시 부하 분산 대상이 되어 request를 처리한다. consumer
  재시작은 없다. local getter 변경만으로 전파 완료를 판정하지 않는다.
- 세부 동작: ChannelName weight 기반 select-one 부하 제외·복원(host 종료 lifecycle 아님).

> **`Weight`와 `Draining` 상태는 다른 축이다.** `Weight = 0`은 ChannelName의 신규 select-one 대상에서
> 해당 membership만 제외한다. `Relocate`와 `Shutdown`의 admission seal은 MeshNode를 신규 자동 선택에서
> 제외할 뿐 아니라 node-local 신규
> application admission도 seal하므로, 호출자가 target을 직접 지정해 우회할 수 없다
> ([Host relocation §7~9](../spec/28-graceful-drain-handoff.ko.md#7-relocation-unit과-실행량-제한)). 이 시나리오는 channel
> request의 weight 축만 검증하며 Actor·Spot `Relocate`·`Shutdown` 동작은 Config 11이 담당한다.

#### RL-B5 ChannelName weight 변경 중 in-flight 완료

우선순위: `P0`

**검증 질문:** ChannelName weight를 0으로 바꾸기 전에 처리 중이던 request는 영향받지 않고 정상 reply로
끝나며, 전파 완료 뒤의 신규 request만 해당 provider의 부하 분산 대상에서 제외되는가.

- 절차: provider가 느린 handler(`value=="slow"`)로 request를 처리하고 있다는 handler-start evidence를
  확인한 뒤 그 provider의 `Channel(channelName).Weight = 0`으로 바꾼다. local getter와
  신규 request의 provider evidence를 함께 확인해 실제 전파 완료를 판정한다.
- 검증: weight 변경 전에 시작한 request는 끝까지 완료되어 reply가 정상 수신된다. 전파 완료 뒤의 신규
  request는 해당 provider evidence에 기록되지 않고 다른 provider가 처리한다. 완료 후 pending이 남지
  않는다. 이 시나리오는 `Draining` 상태 전이, in-flight 대기, actor handoff를 단언하지 않는다.
- 세부 동작: ChannelName weight 변경과 기존 in-flight request의 독립성.

#### RL-B6 부분 degradation (gray failure)

우선순위: `P1`

**검증 질문:** provider 하나가 느려지거나 일부 request에 실패해도 다른 provider의 reply가 오염되지
않고 각 request가 해당 provider의 결과로 유한 시간 안에 끝나는가.

- 절차: provider A는 정상 응답하고 B는 correlation 끝자리가 짝수면 `InternalFailure` error reply, 홀수면
  request timeout보다 길게 응답을 지연하도록 결과가 입력으로 결정되는 fault를 켠다. 두 provider의
  weight를 같은 값으로 유지한 채 target 미지정 request 200개를 최대 동시 실행 수 20으로 보낸다.
  provider handler-start evidence로 각 request의 처리 provider를 식별한다. provider 상태에 따른 자동
  routing이나 circuit breaker를 가정하지 않는다.
- 검증: A와 B가 각각 20개 이상을 처리하고 B가 짝수·홀수 correlation을 각각 하나 이상 받아야 한다.
  A handler-start evidence가 있는 request는 모두 설정한 request timeout 안에 정상 reply로 끝난다. B
  evidence가 있는 request는 주입 규칙과 같은 `InternalFailure` 또는 timeout으로 각각 한 번 끝난다.
  pending이 남지 않고 B의 실패가 A의 payload와 reply correlation을 오염시키지 않는다. framework가 B를
  자동으로 제외하거나 A로 request를 재전송했다고 단언하지 않는다.
- 세부 동작: 고정 routing 조건의 gray failure 격리와 유한 완료.

### Track C — 정리와 partition

#### RL-C1 resource cleanup

우선순위: `P1`

**검증 질문:** 다량의 연결과 요청을 처리한 뒤 정상 종료하면 소켓·핸들·메모리가 누수 없이 정리되는가.

- 절차: 다량의 연결·요청 뒤 client/서버를 정상 종료한다.
- 검증: 소켓·핸들·메모리 등 리소스가 누수 없이 정리된다.
- 세부 동작: 리소스 정리.

#### RL-C2 location store stale descriptor cleanup

우선순위: `P2`

**검증 질문:** provider가 descriptor를 제거하지 못한 채 강제 종료되었을 때 owner lease 만료 후
descriptor가 성공 결과에서 제외되고 consumer가 정상 provider만 선택하는가.

- 절차: provider를 `kill(SIGKILL)`로 비정상 종료해 descriptor remove를 수행하지 못한 상태를 만든다.
- 검증: Owner lease TTL 만료로 그 provider의 Node RID가 public RouteMesh status의 ready peer와
  Channel ready target에서 제외되고, consumer의 follow-up request가 정상 provider에서만
  처리된다. 현재 scale-in 테스트가 graceful `StopAsync`만 다루면 이 경로는 하네스 구현 전까지
  미구현으로 표시한다. lease 만료의 정밀 검증은 Config 6 SF-C1이 담당한다.
- 세부 동작: 비정상 종료 + owner lease 만료 stale 정리.

#### RL-C3 프로세스 정상 정지·새 lifecycle 재시작 후 topology 재수렴

우선순위: `P2`

**검증 질문:** Provider process를 정상 종료했다가 재시작하면 이전 Node RID가 제외되고 새 Node RID로
topology가 다시 수렴하는가.

- 절차: Provider process를 `SIGTERM`으로 정상 종료하고 public RouteMesh status에서 이전 Node RID가
  제외됐는지 확인한다. 그 뒤 같은 endpoint와 automatic discovery가 발급한 새 Node RID로 process를
  재시작하고 public status와 지속 request를 관찰한다.
  실제 network partition과 `SIGSTOP`/`SIGCONT` pause는 연결·lease 조건이 다르므로 이 시나리오에 섞지
  않는다. network partition은 proxy/iptables 같은 별도 harness가 마련되면 별도 시나리오로 추가한다.
- 검증: 종료 구간의 request 결과는 RL-A1의 restart 오류 계약을 따른다. 복구 뒤 새 Node RID가 public
  status에서 Ready가 되고 messaging이 정상화된다. 이전 Node RID는 ready peer·target에 남지 않는다.
  Owner lease, descriptor generation과 endpoint는 public status에 노출하지 않는다.
- 세부 동작: 정상 process restart 뒤 새 lifecycle identity와 topology 재수렴.

#### RL-C4 location store restart/outage 복구

우선순위: `P1`

**검증 질문:** 공유 Location Store가 일시 중단되어도 이미 연결된 ChannelName messaging은 계속
동작하고, Store 복구 후 public topology status가 정상화되는가.

- 절차: provider·consumer가 동작하는 중 store(Redis) 프로세스를 `restart`한다.
- 검증: store 다운 동안 **이미 수립된 MeshNode peer 연결의 messaging은 계속 동작한다**(위치
  resolve는 store에 의존하지만 수립된 연결 자체는 store와 독립 — fail-static). store 다운 중 read
  표면은 Store 장애를 not found가 아니라 infrastructure error로 구분해 반환한다. Store 복구 후
  public RouteMesh status가 다시 Ready로 수렴하며 follow-up
  request가 성공한다. (재등록 순서·owner lease renew 유예·grace 초과 같은 장애 매트릭스 정밀 검증은
  Config 6가 담당한다 — 여기서는 "수립된 연결의 store 독립 + 복구 후 정상화"만 본다.)
- 세부 동작: admitted peer 연결은 store와 독립(fail-static) + 복구 후 owner lease/descriptor 재등록·재조회.

### Track D — 부하와 실패 중 관측

#### RL-D1 high fanout stability

우선순위: `P2`

**검증 질문:** subscriber/consumer가 많은 고fanout 부하에서도 누락·붕괴 없이 안정적으로 처리되는가.

- 절차: 많은 subscriber/consumer로 높은 fanout 부하를 준다.
- 검증: 누락·붕괴 없이 안정적으로 처리된다.
- 세부 동작: 고부하 안정성.

#### RL-D2 observer 실패 격리

우선순위: `P1`

**검증 질문:** dispatch-error observer가 실패해도 messaging 경로는 막히지 않고, 그 실패는
공개 runtime error sink event로 보고되는가.

- 절차: dispatch-error observer 자체가 예외를 던지게 한다.
- 검증: observer 실패가 messaging 경로를 막지 않는다(격리). startup dispatch 설정에 등록한
  runtime error sink가 `event_id=zlink.runtime_error`, `kind=observer_failed`,
  `source=message_flow_observer`를 한 번 받는다. Event에 exception object·stack trace·payload가 없는지도
  확인한다. 일반 logger 문자열이나 언어별 internal helper는 검증 근거로 사용하지 않는다.
- 세부 동작: message-flow observer와 runtime error sink의 격리.

#### RL-D3 로그 marker 관측

우선순위: `P1`

**검증 질문:** 명시적 logging sink를 단 server에서 오류가 나면, 그 sink에 공통
dispatch 오류 marker가 남는가.

- 절차: 명시적 logging provider/sink를 단 server에서 오류·핵심 전이를 발생시킨다.
- 검증: 등록한 logging sink(또는 evidence endpoint)에 `event_id=zlink.dispatch_error`,
  `outcome=failed`, `reason=no_handler`, `action=reply_error`, `packet_name`이 남는다. 이 config의
  server는 logging을 clear하지 않고 marker를 수집할 sink를 명시적으로 단다.
- 세부 동작: 오류 marker 로깅(명시 sink).

#### RL-D4 error reply 직렬화(same-version)

우선순위: `P2`

**검증 질문:** 같은 버전끼리 주고받는 `Error(5)` reply에 error code/message가 wire에 제대로 실려 왕복하는가(client는 message만, code는 raw/evidence로 확인).

- 절차: 같은 버전 provider/consumer 사이에서 다양한 public error를 발생시켜 error reply를 주고받는다.
- 검증: 실패 reply의 wire `message-kind`는 `Error=5`이고 header에 error code/message를 싣는다. 성공 reply는 `Response=2`이며 오류 필드를 싣지 않는다. 별도 `status` 필드는 없다. **raw wire 키는 Web JSON camelCase `errorCode`/`errorMessage`**, 디코드된 .NET 속성명은 `ErrorCode`/`ErrorMessage`다. normal client는 `errorMessage` 기반 예외만 던지고 code는 노출하지 않으므로, client-side는 message 예외만 단언한다. code round-trip은 raw envelope/header(키 `errorCode`) 검사 또는 server-side evidence로 확인한다. dispatch `reason=no_handler`/`action=reply_error`는 wire에 실리지 않으므로 server observer evidence에서 확인한다. 이전 event/status envelope를 받아들이는 호환 decoder는 검증 대상이 아니다.
- 세부 동작: `Response=2`/`Error=5` 분리와 `ErrorCode`/`ErrorMessage` round-trip(동일 버전).

#### RL-D5 지속 혼합 워크로드 soak

우선순위: `P2`

**검증 질문:** 고정한 장시간 기준 부하에서 처리량·latency·오류·pending 추이를 수집해 release 판단에
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

### Track E — service connection liveness

#### RL-E1 orderly disconnect 즉시 반영

우선순위: `P0`

**검증 질문:** RouteMesh와 ClientServer의 FIN, RST 또는 정상 process 종료가 15초 liveness deadline을
기다리지 않고 ready selection에서 제외되는가.

- 절차: ready peer와 server를 각각 정상 종료하고 RST fault도 별도 실행에서 주입한다. Public runtime
  snapshot과 event로 ready 전이를 관찰한다.
- 검증: raw monitor event를 관측한 runtime turn에서 해당 connection을 selection에서 제외한다. Harness의
  5초 observation budget은 process·monitor 전달을 확인하는 상한일 뿐 runtime이 추가로 기다리는 시간이 아니다.
- 세부 동작: raw disconnect와 half-open deadline 분리.

#### RL-E2 RouteMesh·ClientServer half-open 판정

우선순위: `P0`

**검증 질문:** transport disconnect event가 없는 packet blackhole을 application traffic과 독립된 Framework
probe·ACK round trip으로 판정하는가.

- 절차: RouteMesh peer와 ClientServer server 연결에 fault proxy를 두고 한 physical connection의 packet을
  양방향 또는 A→B 한 방향으로 차단한다. 단방향 case에서는 B→A application traffic을 1초마다 계속 보내
  inbound activity가 존재하도록 한다. 같은 topology의 다른 ready target은 정상 통신을 유지한다.
- 검증: Application traffic과 관계없이 5초마다 infrastructure `livenessProbe`가 진행된다. Current connection의
  matching ACK가 15초 동안 없으면 reverse traffic이 계속되어도 차단한 connection만 not-ready가 된다.
  Probe와 ACK는 handler, application mailbox와 message-flow application event에 나타나지 않는다. 다른 ready
  target과 host state는 유지된다.
- 세부 동작: topology 공통 5초/15초 profile과 peer failure 격리.

#### RL-E3 connection lifetime과 stale ACK

우선순위: `P0`

**검증 질문:** probe ID와 ACK가 같은 admitted physical connection에만 적용되고 reconnect 전 connection의
늦은 ACK가 새 deadline을 갱신하지 않는가.

- 절차: protocol fixture에서 non-zero probe ID를 보낸 뒤 connection을 교체하고 이전 connection의 같은 ID
  ACK를 지연 전달한다. 새 connection에는 별도 probe ID를 사용한다. 한 connection의 ACK를 15초보다 짧게
  지연해 5초 tick 재전송도 관찰한다.
- 검증: Service admission이 initial Ready를 만들고 같은 시점에 15초 deadline을 시작한다. Connection마다
  outstanding probe ID는 최대 하나이며 5초 tick에서 ACK가 없으면 새 ID를 계속 만들지 않고 같은 ID를
  재전송한다. Current outstanding ID의 첫 matching ACK만 deadline을 갱신하고 ID를 clear한다. Duplicate,
  이전 ID와 이전 connection ACK는 current round-trip evidence와 ready를 변경하지 않는다. 다른 valid frame은
  diagnostics에는 기록할 수 있지만 ACK를 대신하지 않는다.
- 세부 동작: connection-local probe identity와 stale callback fencing.

#### RL-E4 connection loss와 terminal completion

우선순위: `P0`

**검증 질문:** request admission·reply·timeout·cancellation·disconnect가 경쟁해도 terminal result가 하나이고
다른 peer에 숨은 재제출이 없는가.

- 절차: request가 transport admission 전, admission 직후와 reply 직전에 각각 connection loss와 cancellation을
  경쟁시킨다. Provider evidence는 correlation별 handler 실행 수를 기록한다.
- 검증: caller completion은 정확히 한 번이고 같은 correlation의 handler 실행은 최대 한 번이다. 수락 여부가
  불명확하거나 이미 수락된 request를 다른 peer에 자동 재제출하지 않는다.
- 세부 동작: liveness failure와 request terminal-once 경계.

#### RL-E5 store 독립과 liveness cleanup

우선순위: `P1`

**검증 질문:** store polling 장애 중에도 established connection liveness가 진행되고 terminal host 뒤 timer와
monitor callback이 남지 않는가.

- 절차: ready connection을 유지한 채 Redis 응답을 중단하고 packet blackhole을 별도로 주입한다. 이어서 host
  `PlannedMaintenance` relocation과 `Shutdown`을 각각 완료한다.
- 검증: store failure는 마지막 connection intent를 유지하지만 15초 matching-ACK timeout을 막지 않는다. Transport
  ready도 만료 owner lease를 복구하지 않는다. Terminal 결과 뒤 probe scheduler, reconnect timer, raw monitor
  subscription과 pending callback이 0이다.
- 세부 동작: Location authority와 transport liveness 분리, terminal resource cleanup.

### Track F — Maintenance fencing과 interop

#### RL-F1 preflight·admission seal capacity 경쟁

우선순위: `P0`

- 절차: Target capacity preflight와 source admission seal 사이에 application message를 queue 상한까지
  제출한다. Reversible seal, exact inventory capture, target reservation과 relocation commit의 실행 순서를
  바꾸어 반복한다.
- 검증: 성공으로 반환한 preflight 뒤 accepted work가 reservation 부족으로 유실되지 않는다.
  Reservation이 부족하면 source admission과 descriptor state를 원래대로 복원하고 `Blocked`로 종료한다.
  Target replacement는 target attempt generation과 reservation만 바꾸며 stable relocation ID가 소유한 immutable
  relocation, accepted journal, replay cursor와 operation terminal record를 다시 쓰거나 다른 key로 옮기지 않는다.

#### RL-F2 Actor owner ABA fence

우선순위: `P0`

- 절차: 같은 Actor를 owner A에서 B로 이전한 뒤 새 authority로 A에 다시 이전한다. 최초
  A owner의 message, frozen journal, Message Follow record와 timer를 지연시켜 두 번째 A owner에 도착시킨다.
- 검증: Current membership fence와 다른 모든 record를 application admission 전에 거부한다. 새 A
  owner의 state, accepted journal order과 handler count는 지연 record로 변경되지 않는다.

#### RL-F3 언어 간 terminal failure 해석

우선순위: `P0`

- 절차: Source와 target runtime 언어를 바꿔 reply, infrastructure completion과 relay의 모든 stable
  failure code를 전달한다. 한 회는 schema에 없는 code를 주입하는 음성 fixture로 실행한다.
- 검증: Stable code는 모든 언어 조합에서 같은 typed terminal result로 변환한다. `OK`결과와
  failure code·failure payload의 모순, unknown code와 extra payload는 handler에 전달하지 않고 protocol
  error로 종료한다.

#### RL-F4 ClientServer topology·direction command 격리

우선순위: `P0`

- 절차: 정상 ClientServer client→server 연결을 admission한 뒤 RouteMesh node·Spot·Actor·relocation command와
  server→client application command를 각각 음성 fixture로 주입한다. 동시에 별도 정상 ClientServer 연결에서
  유효한 client→server request를 계속 처리한다.
- 검증: ClientServer role과 direction allowlist에 없는 command는 application handler나 authority state에
  전달하지 않고 해당 physical connection만 protocol error로 종료한다. 잘못된 command를 RouteMesh admission
  또는 server-originated 업무로 해석하지 않으며 다른 정상 연결의 ready와 request completion은 유지한다.

#### RL-F5 Activated seal과 Completed 공개 경계

우선순위: `P0`

- 절차: Actor와 Instance Spot relocation을 target restore·필요한 lifecycle gate·journal replay가 끝난
  `Activated`에서 멈춘다. Standalone Actor의 old Entry membership cleanup은 lifecycle gate에 포함되어 replay
  전에 끝난다. Target application call과 bound-session packet을 제출하고 남은 source resource cleanup 전후에
  source·target을 각각 종료한다. 별도 실행에서는 남은 source resource cleanup을 terminal로 확인한 뒤 authority
  `Completed` CAS를 수행하고 같은 call을
  다시 제출한다.
- 검증: `Activated`에서는 restored target과 session route가 준비되어도 application·session ingress와 public
  ready가 열리지 않는다. Relocation 시작 때 source node와 exact source owner ID·host lease generation을 durable
  subrecord에 고정하며 main owner가 target으로 바뀌어도 이 source token을 유지한다. `Completed` 전 crash는
  immutable relocation을 사용한 replacement 하나로 수렴하고
  target에서 새 업무를 처리한 evidence가 없다. Source cleanup terminal은 current source owner token으로
  인증한 completion 또는 exact source lease expiry를 확인한 coordinator의 fenced CAS로 authority에 먼저
  기록한다. 이 증거와 `Completed` CAS 뒤에만 steady authority로 정규화하고 ready·ingress를 열어 relocation
  reference를 fenced release한다. `Completed` 뒤 owner loss는 종료된 relocation을 replay하지 않고
  일반 owner-loss 결과로 처리한다.

#### RL-F6 admitted descriptor update fence

우선순위: `P0`

- 절차: Admitted current connection에서 descriptor revision을 올리며 weight, runtime state, placement capacity와
  maintenance wave를 각각 갱신한다. 같은 revision의 동일 bytes·다른 bytes, 낮은 revision과 함께 MeshName,
  security identity, endpoint, node lifecycle generation, negotiated message bound, Channel membership key와
  capability identity를 바꾼 update fixture도 전달한다.
- 검증: 더 높은 revision의 허용된 mutable field만 적용한다. 같은 revision·same bytes는 idempotent이고 낮은
  revision은 stale로 무시한다. 같은 revision·different bytes 또는 immutable identity·capability mutation은
  application handler와 selection에 적용하지 않고 offending connection을 not-ready와 protocol error로
  종료한다. Reconnect한 새 physical connection은 service admission을 처음부터 다시 수행한다.

#### RL-F7 relocated request reply ACK barrier

우선순위: `P0`

- 절차: 서로 다른 reply correlation을 가진 node·Channel·Spot·Actor·Instance·bound-session accepted request를
  Relocation journal에 포함하고 target에서 처리해 reply를 만든다. `replyRelay`,
  `replyRelayAck`와 ACK 재전송을 각각 한 번씩 유실한다. 다른 반복에서는 원 caller의 timeout·cancellation,
  request-source connection 종료·재연결, request-source host lease expiry와 reply 도착을 경쟁시킨다.
- 검증: Target은 stable relocation ID, exact request-source owner fence와 operation ID로 terminal completion을
  relocation stream에 기록하고 current
  request record에 보존한 exact nonzero reply route로 응답한다. Operation ID를 reply route로 대신하지 않으며
  request 종류마다 원 correlation으로 terminal result 하나가 도착한다. Current request-source connection의
  `terminalReceived` 또는 `alreadyTerminal` ACK까지 relay를 재전송한다. ACK가
  유실되면 source가 이미 terminal이어도 같은 결과로 다시 ACK하며 application completion은 한 번뿐이다.
  Physical connection close는 terminal 증거가 아니므로 current route에서 relay를 계속한다. 모든 accepted
  request가 ACK되거나 accepted record에 고정한 exact request-source owner lease expiry로 caller terminal이
  확정되기 전에는 source cleanup terminal, authority `Completed`와 relocation release를 수행하지 않는다.
  Source lease가 유지된 partition이 host deadline을 넘으면 `ForceStopped`로 끝내되 reply bytes와 relocation은
  recovery retention 동안 유지한다.

#### RL-F8 manual source의 accepted work와 maintenance capture

우선순위: `P0`

- 절차: Object role `None`이고 Location Store와 Relocation Store를 사용하지 않는 fixed-RID manual RouteMesh
  peer에서 target object로 장기 request와 one-way send를
  각각 수락시킨 뒤 automatic topology만 등록한 target host에 `PlannedMaintenance` mode의 `Relocate`를
  시작한다. Target host에는 manual RouteMesh peer, ClientServer client endpoint와 manual fanout component를
  등록하지 않는다. 첫 반복은 두 작업을
  capture 전에 완료하고, 두 번째 반복은
  reversible seal deadline을 넘도록 handler를 지연한다. Manual peer를 재시작해 같은 RID로 새 service
  connection도 admission한다.
- 검증: Manual peer lifecycle generation은 runtime이 만든 nonzero opaque equality token이며 숫자 대소로 restart를
  판정하지 않는다. Current physical connection handover가 이전 connection event를 fence한다. Connection-bound
  request와 one-way send는 durable accepted journal과 reply relay에 포함하지 않고 모두 `Captured` 전에 terminal
  완료한다. 하나라도 deadline 안에 끝나지 않으면 relocation은 pre-Captured abort, `Relocate`는
  `Blocked/DeadlineExceeded`이고 source admission을 복원한다. Durable accepted journal은 exact source owner lease로
  fence할 수 있는 lease-backed accepted work만 기록한다. 이 시나리오는 relocating host에 manual component를
  등록한 경우를 허용하지 않는다. 그 경우에는 Config 11 OBS-C9처럼 precommit에서
  `Blocked/ManualTopologyUnsupported`여야 한다.

#### RL-F9 preflight deadline과 seal 경계

우선순위: `P0`

- 절차: 첫 반복은 target capability·capacity preflight를 host deadline 뒤까지 지연하되 source admission seal은
  시작하지 않는다. 두 번째 반복은 preflight와 seal을 완료한 뒤 teardown을 같은 deadline 뒤까지 지연한다.
- 검증: Seal 전 timeout은 `Blocked/DeadlineExceeded`로 한 번 끝나고 host·descriptor·admission은 호출 전 상태를
  유지한다. Seal 뒤 timeout만 `ForceStopped/DeadlineExceeded`이며 bounded teardown을 수행한다. 두 결과가 같은
  reason을 사용하더라도 outcome과 state transition을 바꾸어 해석하지 않는다.

#### RL-F10 Entry Actor와 SpotWide User Spot 이전

우선순위: `P0`

- 절차: 첫 반복은 `Snapshot` policy와 Actor RelocationAdapter를 등록한 Actor를 source Entry Spot에 둔 뒤 host
  `PlannedMaintenance` relocation을 실행한다. Target offer와 Prepared
  evidence에 target node의 initialized Entry Spot identity를 기록하고 NewOwner CAS 전후 authority를 관찰한다.
  두 번째 반복은 `SpotWide` 실행 모델, `Snapshot` policy와 Spot RelocationAdapter를 등록한 User Spot에
  `Snapshot` Actor를 join한
  상태로 `PlannedMaintenance` mode의 `Relocate`를 호출한다. 두 target은 같은 stable type capability,
  adapter capability와 aggregate 전체를
  수용할 capacity를 게시한다.
- 검증: Entry member Actor는 ObjectGeneration을 유지하면서 owner node, AuthorityOwnerGeneration과 current Spot을
  target Entry Spot ID·ObjectGeneration·kind로 한 CAS에서 바꾼다. Target factory가 만든 Actor에 Snapshot
  Adapter의 `Restore`와 journal staging을 끝낸 뒤 authority를 commit한다. Infrastructure relocation은
  application의 membership 변경이 아니므로 target Entry Spot의 join·relocation callback과 source Entry
  Spot의 leave callback을 호출하지 않는다. Source membership의 durable cleanup 뒤 accepted journal을
  replay한다. Source process 종료 반복에서도 fenced durable source cleanup terminal 뒤에만 replay를 시작한다.
  Maintenance 반복에서 target `OnActorJoin`·`OnJoinedActor` 또는 source `OnLeaveActor`가 한 번이라도 호출되거나
  source cleanup보다 journal replay가 먼저 시작되면 실패다.
  이 `Restore` 검증은 `Snapshot` participant에만 적용하며 RL-F10에 `Recreate` 반복을 섞지 않는다.
  Callback·replay·cleanup은 current attempt에서 retry-safe하며 source cleanup 뒤 `Completed`, bound-session
  route ACK와 steady normalization 전까지 admission을 닫는다. User Spot member가 있는
  반복은 Spot과 current member Actor 전체의 Snapshot payload와 accepted journal을 하나의 immutable relocation
  root에 저장하고 membership을 유지한 채 owner·membership aggregate commit에 성공한다. User Spot aggregate는
  Actor `OnActorJoin`·`OnJoinedActor`·`OnLeaveActor` evidence가 모두 0건이어야 하며 aggregate
  journal은 commit 뒤에 replay한다. 일반 join용 `OnJoinedActor`를 maintenance 완료 신호로 사용하면 실패다.
  `RelocationDisabled` blocker가 필요하면 이 성공 반복을 바꾸지 않고 별도의 `Disabled` participant fixture로
  preflight abort와 source authority 보존을 검증한다.

#### RL-F11 readiness-first relocation과 느린 turn 격리

우선순위: `P0`

- 절차: 한 source Framework process에 standalone Actor, User Spot aggregate와 Instance Spot을 합쳐 80개의
  relocation unit을 배치한다. 일부 unit의 현재 turn은 network I/O barrier에서 지연하고 나머지는 즉시
  완료되게 한 뒤 `PlannedMaintenance` mode의 `Relocate`를 시작한다. Framework relocation notification이
  각 execution queue에 도달한 시각,
  turn boundary, permit 획득, seal과 relocation terminal marker를 기록한다.
- 검증: 현재 turn을 끝낸 unit부터 종류와 등록 순서에 관계없이 relocation을 시작한다. 느린 unit은
  `WaitingTurn`인 동안 기존 owner에서 업무를 계속 처리하며 ready unit을 막지 않는다. Process 전체 active
  outbound·inbound relocation은 각각 64를 넘지 않고, permit을 얻지 못한 unit에는 seal·hold queue가 생기지
  않는다. User Spot과 member Actor는 하나의 aggregate permit과 commit generation을 사용하며 개별 Actor가
  먼저 공개되지 않는다.
- 세부 동작: Framework queue notification, readiness-first 선택, bounded sliding concurrency와 aggregate 단위.

#### RL-F12 User Spot queue·Message Follow·timer 자동 복원

우선순위: `P0`

- 절차: Snapshot User Spot과 member Actor에 periodic timer를 등록한다. Source User Spot의 현재 turn `R0`를
  barrier에서 지연하고, 아직 실행하지 않은 direct message `Q1 -> Q2`, Actor message `A1 -> A2`와 pending
  timer tick을 queue에 수락시킨다. Relocation notification 뒤 `R0`만 완료시킨다. Source seal을 확인한 다음
  이전 owner route로 request와 one-way `H1 -> H2`를 추가 제출하고 target restore·authority commit·route
  normalization을 순서대로 진행한다.
- 검증: Framework는 `R0` 뒤 다음 application turn을 source에서 실행하지 않는다. `Q1`, `Q2`, `A1`, `A2`,
  logical timer registration과 pending tick은 application state와 함께 immutable relocation payload에 저장된다.
  Target은 native timer handle을 새로 만들고 application이 timer를 다시 등록하지 않아도 timer name, interval,
  overrun policy, 마지막 완료 sequence와 다음 예정 시각을 복원한다. Application 실행 순서는 frozen queue와
  pending tick의 원래 ordering boundary를 보존한 뒤 `H1 -> H2`, Ready 이후 target이 직접 수락한 message
  순서이며 각 operation과 tick은 정확히 한 번만 관측된다.
- 세부 동작: 현재 turn만 source에서 완료, 미실행 queue와 logical timer의 durable relocation, seal 중 ingress
  hold와 commit 뒤 Message Follow.

#### RL-F13 relocation count·callback·payload permit

우선순위: `P0`

- 절차: 첫 반복은 encoded relocation payload가 각각 약 1 MiB인 80개의 standalone Actor를 동시에 ready로
  만든다. 두 번째 반복은 64 MiB payload를 반환하는 Actor를 여덟 개 준비한다. Capture·Restore adapter와
  instrumented Relocation Store가 active operation, callback concurrency, seal 전 byte reservation과 Capture 뒤
  actual encoded bytes in flight의 high-water를 기록한다. 별도 반복에서는 participant별 Snapshot state의
  64 MiB reservation과 Framework-owned encoded upper bound를 합하면 256 MiB를 넘는 하나의 User Spot aggregate를
  준비한다. Aggregate가 exclusive permit을 기다리는 동안 normal unit도 계속 ready로 만들고 permit 획득 실패를
  반복한다. 마지막 반복에서는 adapter가 64 MiB보다 1 byte 큰 결과를 반환한다.
- 검증: 기본 설정에서 active outbound·inbound relocation high-water는 각각 64 이하이고 Capture·Restore
  callback은 각각 8 이하이며 encoded payload in flight는 256 MiB 이하이다. 1 MiB 반복은 active unit 64개까지
  진행하고 나머지는 source admission을 유지한다. 64 MiB 반복은 payload 단계가 동시에 네 개를 넘지 않는다.
  256 MiB를 넘는 reservation의 단일 aggregate는 payload window가 빈 뒤에만 exclusive하게 진행하고 다른
  relocation과 겹치지 않는다. Permit attempt가 실패할 때 unit·callback permit high-water가 남지 않으며 normal
  unit과 aggregate가 서로를 기다리지 않고 deadline 안에 terminal 상태에 도달한다. Count·callback·byte permit을
  모두 확보하기 전에 해당 unit의 source queue를 seal하면 실패다. Seal 전 reservation은 participant별 64 MiB와
  Framework-owned section의 deterministic encoded upper bound보다 작지 않고, Capture 뒤 permit은 actual encoded
  size로 감소하지만 증가하지 않는다. 64 MiB를 넘긴 adapter 결과는 relocation root나 owner commit을 만들지 않고
  precommit abort와 source normalization 뒤 `Blocked/StateIncompatible`로 끝난다.
- 세부 동작: 기본 `64/64`, `8/8`, 256 MiB의 독립 gate와 oversized aggregate 단독 실행.

#### RL-F14 precommit abort의 frozen·hold queue 복원

우선순위: `P0`

- 절차: User Spot의 현재 turn을 완료한 뒤 frozen queue `Q1 -> Q2`와 seal 중 hold queue `H1 -> H2`를 만든다.
  Target reservation failure와 Restore failure를 authority commit 전에 각각 주입한다. Abort authority CAS,
  route abort ACK와 source steady normalization을 단계별로 지연한다.
- 검증: Abort가 durable하게 확정되기 전에는 source admission을 열지 않는다. 정상화 뒤 source는
  `Q1 -> Q2 -> H1 -> H2` 순서와 original operation ID·reply route를 보존해 정확히 한 번 처리한다. Frozen
  queue나 hold queue를 다른 owner에 숨겨서 제출하지 않고, timer registration과 pending tick도 source의 같은
  logical schedule로 복원한다. Abort cleanup 뒤 relocation payload와 target staging이 남지 않는다.
- 세부 동작: relocation 전 실패에서 queue·timer·reply identity의 source 복원.

## 5. 완료 기준

- 모든 `P0`와 connection liveness 묶음 `RL-E1`~`RL-E5`는 네 runtime lane과 다섯 public 언어 표현에서
  구현한다. 모든 `P1`은 해당 formal capability를 지원하는 언어에서 구현한다. `P2`는 선택이며 미구현
  이유를 남긴다.
- 복구 시나리오는 복구 후 follow-up request 성공과 public RouteMesh status의 ready peer·target
  제거·추가로 관측한다. 내부 pending, stale descriptor와 provider record는 public 표면이 아니므로
  직접 단언하지 않는다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
