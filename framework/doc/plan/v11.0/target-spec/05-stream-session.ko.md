# STREAM session service

이 문서는 ZLink Framework 11.0의 server-side STREAM packet session, lifecycle, typed dispatch, Actor
binding, reverse push와 transfer barrier를 정의한다. Client connector는 같은 internal packet protocol과 reply
correlation을 사용한다.

## 1. 책임 경계

Raw STREAM은 transport connection, peer routing identity와 byte·packet 전달을 담당한다. Framework STREAM
session은 connection별 lifecycle, fixed packet header, typed handler dispatch, request correlation과 Actor binding을
담당한다. Application은 raw recv loop, arbitrary chunk와 transport handle을 service API에서 다루지 않는다.

Packet header binary layout, request sequence, binding token과 Actor route는 Framework protocol이다. Application은
packet name, typed payload, immutable metadata와 session context만 사용한다. Header 형식, codec number와 request
sequence를 직접 구성하거나 바꾸는 public option을 제공하지 않는다.

STREAM-only node는 MeshNode를 요구하지 않는다. Actor dispatch를 사용하는 STREAM node는 startup에서 target
MeshName 하나와 같은 process의 local MeshNode 하나를 명시한다. Endpoint나 첫 MeshNode에서 MeshName을
추론하지 않으며 서로 다른 MeshName 사이에 relay와 fallback을 제공하지 않는다.

## 2. Registration과 startup

STREAM node는 stable node name, bind endpoint와 session type 하나를 명시적으로 등록한다. Attribute나 decorator
기반 integration을 제공하는 언어도 결과적으로 node 하나에 session type 하나만 확정해야 한다.

다음 조건은 listener를 열기 전에 configuration 오류로 끝난다.

- Node name 또는 bind endpoint가 비어 있음
- 같은 node name이나 session type을 중복 등록함
- 한 node에 둘 이상의 session type을 등록함
- Actor dispatch를 사용하지만 target MeshName 또는 local MeshNode가 없음
- TLS를 활성화했지만 certificate와 key가 모두 제공되지 않음
- TLS server 없이 client certificate requirement를 설정함

Bind host와 advertised host, automatic port allocation은 listener identity 공통 계약을 따른다. STREAM endpoint는
MeshNode, ClientServer 또는 fanout descriptor에 기록하지 않는다.

## 3. Session lifecycle

Transport connection이 identity와 protocol handshake를 통과한 뒤 session scope를 만들고 connected callback을
managed application queue에서 실행한다. Transport callback thread에서 application callback을 직접 실행하지
않는다.

Session은 `Created`, `Connected`, `Closing`, `Closed`의 단조 lifecycle을 가진다. 같은 connection의 packet
handler, lifecycle callback과 session-owned state는 session turn에서 순서를 정한다. Request completion,
send-ready, binding update, heartbeat와 disconnect cleanup은 infrastructure task에서 진행한다.

Disconnect는 신규 packet admission을 닫고 accepted callback, reply와 binding cleanup을 deadline까지 처리한 뒤
disconnected callback을 한 번 실행한다. Late packet과 reply는 새 connection이나 새 session scope에 전달하지
않는다. Reconnect는 새 session identity, correlation registry와 binding token을 사용한다.

Session 오류 callback은 그 session에 귀속되는 transport 오류만 받는다. Handshake failure와 socket·node
단위 오류는 runtime monitoring이 처리한다. Application handler exception은 session transport 오류로 바꾸지
않는다.

## 4. Packet dispatch와 reply correlation

Framework는 complete packet header와 payload를 검증한 뒤 packet name과 message kind로 typed handler를
선택한다. Payload 일부나 malformed header를 handler에 전달하지 않는다. Packet name별 message type은 root codec
registry의 typed serializer를 사용하며 handler마다 codec을 등록하거나 decode helper를 반복 전달하지 않는다.

Request의 `Response`와 `Error`는 원본 request sequence만 사용해 caller의 pending operation을 찾는다. Response
header에 packet name을 넣지 않는다. Typed reply type은 client request call이 정하며 response packet name으로
다시 선택하지 않는다.

Reply, timeout, cancellation, disconnect와 shutdown이 경쟁하면 terminal result 하나만 확정한다. Timeout이나
disconnect 뒤 같은 request를 다른 session, Actor 또는 owner에 자동 재제출하지 않는다. Session close 뒤 늦은
reply를 새 binding으로 전달하지 않는다.

One-way send와 explicit reply는 source transport admission까지만 기다린다. Remote callback 완료는 submit
결과가 아니다. Reply ownership은 첫 유효한 terminator가 한 번 획득하며 admission failure나 cancellation 뒤에도
재사용하지 않는다.

## 5. Actor binding authority

Binding은 MeshName, Actor identity·ObjectGeneration, current AuthorityOwnerGeneration, OwnerLeaseGeneration,
STREAM session identity와 binding token의 runtime 관계다. 한 Actor ObjectGeneration은 동시에 session 하나에만
bind할 수 있고 session 하나는 여러 Actor를 bind할 수 있다.

Bind는 Actor ObjectGeneration, current owner route, AuthorityOwnerGeneration과 OwnerLeaseGeneration을 검증한 뒤
새 binding token을 발급한다.
같은 current binding을 다시 요청하면 idempotent result를 반환할 수 있다. 다른 session의 active binding과
경쟁하면 기존 binding을 암묵적으로 해제하지 않고 conflict로 끝난다.

Rebind는 새 token을 발급하고 이전 token을 무효화한다. Unbind와 close는 expected token 또는 binding generation을
비교해 current binding만 변경한다. Stale token, ObjectGeneration, AuthorityOwnerGeneration 또는
OwnerLeaseGeneration의 dispatch, reply, push와 close는 current binding에 적용하지 않는다.

Bind와 rebind는 새 binding route와 token을 한 번에 commit한다. Validation, timeout, disconnect, Store failure나
target owner 변경으로 commit하지 못하면 기존 binding과 token을 그대로 유지한다. Unbind는 current token이
일치할 때만 binding을 제거하고, 이미 없는 binding을 같은 expected absence로 해제하는 호출은 idempotent
success다. 다른 token의 binding은 변경하지 않는다.

Bind, rebind, unbind와 bound-session close는 reply, timeout, cancellation, disconnect와 경쟁해도 terminal result
하나만 반환한다. Caller cancellation은 waiter만 끝내며 이미 commit된 binding mutation을 되돌리지 않는다.
Bound-session close는 current binding을 먼저 seal하고 해당 connection close가 commit된 뒤 binding을 한 번
제거한다. Connection close를 commit하지 못하면 기존 binding을 유지하거나 current connection 상태에 맞는
terminal failure를 반환하며, 다른 session에 같은 operation을 자동 재제출하지 않는다.

Binding route는 Framework가 관리한다. Application이 별도 Location row, proxy, session RID와 target endpoint를
만들지 않는다. Bound-session API는 current Actor의 binding으로 push를 보내거나 connection close를 요청하며
임의 session을 지정하는 전역 proxy를 제공하지 않는다.

## 6. Session에서 Actor로 dispatch

Session handler가 Actor dispatch를 선택하면 Framework는 binding token, Actor ObjectGeneration,
AuthorityOwnerGeneration, OwnerLeaseGeneration, session sequence와 original request correlation을 내부
envelope에 보존한다. Payload는 local·remote 여부와
관계없이 target Actor application queue에 직접 추가한다.

같은 session과 Actor binding에서 accepted된 packet은 Actor queue에서 FIFO를 유지한다. 서로 다른 session
사이의 global order는 보장하지 않는다. Actor handler reply는 original STREAM request correlation을 terminal
result 하나로 완료한다.

Binding이 없으면 session-not-bound, Actor route가 없으면 route-not-connected, source admission capacity가
없으면 backpressure 또는 timeout으로 끝난다. Route failure와 실행 여부가 불명확한 timeout 뒤 다른 Actor,
owner와 MeshNode를 자동 선택하지 않는다.

Session handler는 Spot state를 직접 변경하지 않는다. Actor call 또는 explicit Spot call을 제출하고 각각의
owner turn에서 결과를 처리한다.

## 7. Actor에서 bound session으로

Actor-originated push는 current binding을 한 번 snapshot하고 complete packet을 session transport에 제출한다.
Binding이 없거나 token이 stale이면 다른 session을 찾지 않고 session-not-bound 또는 stale-binding으로 끝난다.
Connection이 닫혔으면 route-not-connected, send capacity가 없으면 bounded admission 결과를 반환한다.

Actor-originated close는 current binding token과 generation을 비교해 해당 connection을 닫고 binding을 한 번
정리한다. Stale Actor owner, AuthorityOwnerGeneration, OwnerLeaseGeneration과 token은 새 connection을
닫지 못한다.

같은 Actor binding에서 accepted된 push는 session FIFO를 유지한다. Request/reply와 push 사이에 application
global order를 추가로 보장하지 않으며 protocol이 정한 session sequence만 사용한다.

## 8. Actor transfer barrier

Actor transfer가 시작되어도 physical STREAM connection과 session scope는 session owner process에 유지된다.
Source Actor admission seal과 함께 active binding별 마지막 accepted session sequence를 barrier에 기록한다.

Barrier 전에 accepted된 packet은 source turn 또는 durable transfer journal에서 Actor binding별 sequence 순서로
처리한다. Handler 완료 뒤 replay cursor commit 전에 recovery process가 종료되면 마지막 미확정 packet을 같은
operation ID로 다시 실행할 수 있다. Request terminal completion은 한 번만 확정한다.
Barrier 뒤 수락 가능한 packet은 bounded pending allowance 안에서만 보관한다. Allowance가 소진되면 raw session
ingress admission을 중단하고 추가 call을 backpressure한다. 수락하지 않은 packet은 transfer 대상이 아니다.

Location authority commit 뒤 target Actor가 state와 journal을 restore하고 activation barrier를 끝낼 때까지
binding route를 application dispatch에 공개하지 않는다. Target은 participant별 terminal sequence까지 연속으로
staging하고 source와 합의한 barrier를 확인한다. Target admission은 transferred range보다 새 owner
generation의 packet을
먼저 실행하지 않는다.

Commit 전 failure는 source binding route를 유지하고 pending packet을 원래 sequence로 복구하거나 명확한
terminal 오류로 끝낸다. Commit 뒤에는 source route로 rollback하지 않는다. Recovery coordinator가 target
activation과 binding route publication을 이어서 수행한다.

Session disconnect는 accepted packet의 terminal sequence를 처리한 뒤 participant를 종료한다. Disconnect
자체를 barrier completion으로 간주하지 않는다. 이전 owner, AuthorityOwnerGeneration,
OwnerLeaseGeneration과 binding token의 packet, reply와 push는 target activation 뒤에도 거부한다.

## 9. 동시성, 종료와 오류

서로 다른 session의 dispatch와 binding operation은 병렬로 실행할 수 있다. 같은 session의 handler turn,
binding mutation, close와 transfer barrier는 session owner가 직렬화한다. Callback 안에서 같은 session의
shutdown 또는 destructive close를 동기적으로 기다리면 deadlock 오류다.

Session owner host가 `Retire` 또는 `Shutdown`을 시작하면 physical connection을 다른 process로 이동하지
않는다. 신규 session과 bind를 거부하고 accepted callback, reply와 binding cleanup을 deadline까지 처리한 뒤
connection을 닫는다. Client가 연속 연결을 필요로 하면 application protocol의 reconnect를 사용한다.

Actor owner host의 `Retire`는 §8의 binding route barrier를 사용한다. `Shutdown`은 새 Actor transfer를
시작하지 않으며 진행 중인 barrier만 deadline까지 정리한다.

Framework STREAM heartbeat는 application packet session의 liveness control이다. Framework service connection
liveness, Location owner lease와 request timeout과는 다른 계약이며 서로 대신 사용하지 않는다.

## 10. 검증 요구

- Session lifecycle과 packet handler가 transport callback thread에서 직접 실행되지 않는다.
- Complete header·payload와 peer identity가 session dispatch까지 손실 없이 전달된다.
- Response와 Error가 request sequence만으로 original request를 한 번 완료한다.
- Actor payload가 Spot callback을 거치지 않고 Actor queue로 직접 전달된다.
- 한 Actor generation이 session 하나에만 bind되고 stale token이 current binding을 바꾸지 않는다.
- Actor transfer에서 physical connection은 이동하지 않고 route, AuthorityOwnerGeneration과 owner
  lease fence가 갱신된다.
- Barrier 전 accepted packet이 유실·역전되지 않고 crash 뒤 미확정 replay가 같은 operation ID를 유지하며 target
  activation 뒤 새 route를 사용한다.
- Session owner 종료가 connection을 이동하지 않고 bounded cleanup과 client reconnect로 끝난다.
- Handler, heartbeat, binding, disconnect와 socket close 경쟁 뒤 runtime-owned resource가 남지 않는다.
