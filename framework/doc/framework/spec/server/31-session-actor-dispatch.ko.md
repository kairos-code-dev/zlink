# Session Actor Dispatch — 공통 스펙

[스펙 목차](../README.ko.md) · [STREAM 서버 세션](30-stream-session.ko.md) ·
[Actor 모델](22-actor-model.ko.md) · [Spot과 Actor membership](23-spot-actor.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 STREAM session과 Actor runtime을 연결하는 typed dispatch, binding,
owner handoff와 ordering을 정의한다. Core raw transport는 Actor identity, binding token, AuthorityOwnerGeneration,
sequence barrier와 route를 해석하지 않는다.

`EnableActorDispatch()`는 MeshName을 받지 않고 global object dispatch capability를 연다. Startup은 같은
process에 Object Client 또는 Server role과 Location Store가 하나 이상 구성됐는지 확인한다. 여러 Mesh가
구성되어 있어도 그 자체는 오류가 아니며 global ActorId authority가 current Mesh와 owner를 resolve한다.
STREAM-only node에서 Actor dispatch를 사용하지 않으면 MeshNode를 요구하지 않는다.

Object role이 `None`이거나 Location Store가 없으면 Actor dispatch enablement를 startup에서 거부한다. Hidden
same-process Actor authority나 local-only binding 의미를 제공하지 않는다.

## 2. Public flow

Application은 session object, ActorRef, typed payload·reply와 bound-session API만 사용한다. Node RID, STREAM
transport handle, raw relay envelope, request sequence, AuthorityOwnerGeneration과 endpoint를 조립하거나 보관하지
않는다.

1. Session callback이 client를 인증하고 domain Actor identity와 type을 정한다.
2. Global ActorId로 Ready ActorRef를 lookup하거나 application 정책에 따라 Actor를 명시적으로 생성한다.
3. Session과 Actor를 binding token으로 bind한다.
4. Session handler가 typed payload를 current Actor route로 제출한다.
5. Actor handler는 typed reply를 반환하거나 current bound session으로 one-way push를 보낸다.

## 3. Inbound dispatch와 reply

STREAM packet은 session typed handler registry로 먼저 dispatch된다. Actor dispatch를 선택하면 Framework는 original
request correlation, binding token, Actor ObjectGeneration, AuthorityOwnerGeneration, OwnerLeaseGeneration과 session
sequence를 internal envelope에 보존한다.

Payload는 local·remote 여부와 관계없이 target Actor application queue에 직접 추가한다. Current Spot은 authority
검증에 사용하지만 callback 실행 문맥이 아니다. Session callback thread에서 Actor handler를 실행하지 않으며 서로
다른 Actor를 session 또는 Spot global queue로 직렬화하지 않는다.

Request reply·error는 original STREAM correlation을 terminal-once로 완료한다. Timeout, cancellation과 실행 여부가
불명확한 route failure 뒤 다른 Actor·owner·MeshNode로 자동 재제출하지 않는다. Session close 뒤 late reply를 새
session이나 binding에 전달하지 않는다.

## 4. Binding authority

Binding은 exact `ActorRef`의 ActorId·ObjectGeneration, current AuthorityOwnerGeneration·OwnerLeaseGeneration,
STREAM session identity,
binding generation과 token의 runtime 관계다. 한 Actor는 동시에 session binding 하나만 가지며 session 하나는 여러
Actor를 bind할 수 있다.

Bind는 caller가 제출한 ActorRef 위치로 control request를 한 번 보내고 current Ready Actor authority를 exact
검증한 뒤 새 token을 발급한다. Source는 bind 전에 Store에서 current route를 선조회하지 않는다. Local Actor
instance overload는 제공하지 않는다. Rebind는 새 token을 발급하고 이전 token을 무효화한다. Unbind와 close는
expected token 또는 binding generation을 비교해 current binding만 바꾼다.
이전 token, ObjectGeneration, AuthorityOwnerGeneration 또는 owner lease의 dispatch, reply, push와 close는 current
binding에 적용하지 않는다.

Target에 exact Actor가 없고 active committed forwarding mapping이 있으면 original bind control request와 reply
route를 mapping target으로 relay한다. Mapping이 없거나 만료됐으면 `ActorLocationStale`, 같은 ActorId의
ObjectGeneration이 다르면 `ActorGenerationStale`, relocation pre-commit seal 중이면 `ActorMoving`으로 끝난다.
Source는 Store에서 새 route를 찾아 같은 bind를 hidden retry하지 않는다. `BindOrGet`의 Get은 같은 session의
exact ActorId·ObjectGeneration binding만 반환하며 다른 generation이나 directory Actor를 반환하지 않는다.

Binding route는 Framework가 관리한다. Application이 별도 Location row, proxy, session RID와 endpoint를 만들지
않는다. Bound-session API는 current binding으로 one-way push를 보내거나 connection close를 요청하며 임의 session을
지정하는 global proxy를 제공하지 않는다. Disconnect는 binding을 해제하지만 Actor를 destroy하거나 membership을
바꾸지 않는다.

## 5. Actor relocation route barrier

Actor가 다른 MeshNode로 이동해도 physical STREAM connection과 session scope는 session owner process에 유지된다.
Socket, transport handle과 session callback state를 target Actor process로 이동하거나 복제하지 않는다.

1. Source Actor seal과 함께 current AuthorityOwnerGeneration, binding generation과 마지막 accepted session sequence를
   barrier에 기록한다.
2. Session owner는 ingress를 reversible하게 seal하고 exact high-water를 ACK한다.
3. Bound-session request는 Captured CAS 전에 terminal drain하며 durable journal에 넣지 않는다.
4. Lease-backed one-way packet만 negotiated boundary 안에서 relocation envelope에 포함할 수 있다.
5. Target은 Relocation Store root를 restore하고 accepted journal을 실행하지 않은 staging queue로 준비한다. 새 route를
   stage하지만 switch·unseal하지 않는다.
6. Durable source cleanup과 Completed authority CAS 뒤 target이 session route commit을 보낸다.
7. Session owner는 exact Actor ObjectGeneration, 이전·target AuthorityOwnerGeneration, binding generation,
   session owner lease와 high-water를 검증해 route를 atomic switch하고 routed ACK를 보낸다.
8. Owner commit 뒤 lifecycle callback과 accepted journal replay를 완료하고 maintenance authority를 steady target으로
   normalize한 뒤에만 target Actor packet·push admission을 연다.

Activated, Cleaning과 Completed만으로 route나 admission을 열 수 없다. 이전 owner, stale authority owner generation,
binding token과 sequence의 packet·reply·push·close는 current connection에 적용하지 않는다.

## 6. Failure와 recovery

Commit 전 failure는 durable Aborted CAS 뒤 session abort route와 ACK, cleanup, steady source normalization 순서로
source route를 복원한다. Aborted CAS 전 route를 바꾸거나 ingress를 다시 열지 않는다.

Commit 뒤에는 source route로 rollback하지 않는다. Current target 또는 recovery coordinator가 activation,
Completed, route switch ACK와 steady normalization을 이어간다. Session owner process가 종료되면 connection을 다른
process로 복구하지 않고 닫으며 client reconnect가 새 session을 만든다.

Physical disconnect는 accepted participant high-water, request terminal completion 또는 relocation cleanup의 증거가
아니다. Connection-bound work와 bound-session request가 pre-Captured deadline 안에 terminal drain되지 않으면
relocation을 abort하고 `Blocked/DeadlineExceeded`로 admission을 복원한다.

## 7. Execution과 lifecycle

같은 session의 handler turn, binding mutation, close와 relocation barrier는 session owner가 직렬화한다. Actor에
제출한 뒤에는 Actor queue가 순서를 소유한다. Session turn과 Actor turn을 shared lock이나 callback stack으로 합치지
않는다.

Request completion, send-ready, binding update, relocation barrier와 disconnect cleanup은 infrastructure task에서
진행한다. Session 또는 Actor application callback이 비동기 작업을 기다리는 동안에도 진행해야 한다.

Actor owner host의 Retire는 §5 barrier를 사용한다. Session owner host의 Retire와 Shutdown은 신규 session·binding을
거부하고 accepted callback·reply·cleanup을 deadline까지 처리한 뒤 connection을 닫는다. Physical connection을
다른 process로 이동하지 않는다.

## 8. Startup과 operation errors

| Condition | Result |
|---|---|
| Object Client·Server role 없음 | Configuration error |
| Location Store 없음 | Configuration error |
| Mapping 없는 stale ActorRef 위치 | `ActorLocationStale` |
| 다른 ObjectGeneration | `ActorGenerationStale` |
| Relocation pre-commit seal | `ActorMoving` |
| 같은 packet key handler 중복 | Configuration error |
| Actor factory 없음 | Explicit create error |
| Current binding 없이 push·close | Session-not-bound |
| Stale Actor·owner·binding fence | Typed stale error; no fallback |

## 9. 검증 요구

- Actor dispatch enablement가 MeshName을 받지 않고 global Actor authority를 사용한다.
- Object role 또는 Store가 없으면 startup에서 거부하고 local-only binding을 만들지 않는다.
- Local·remote payload가 Actor queue로 직접 전달되고 Spot callback을 거치지 않는다.
- Bind가 exact ActorRef를 한 번 제출하고 stale route를 hidden Store retry하지 않는다.
- Rebind 뒤 이전 token과 authority fence가 current binding을 바꾸지 않는다.
- Request reply가 original STREAM correlation으로 한 번 완료된다.
- Physical STREAM connection과 session object를 Actor target process로 이동하지 않는다.
- Bound-session request가 Captured 전에 terminal drain되고 durable journal에 들어가지 않는다.
- Completed route switch ACK와 steady normalization 전 target admission이 닫혀 있다.
- Commit 전 failure는 source route, commit 뒤 failure는 target recovery로 수렴한다.
