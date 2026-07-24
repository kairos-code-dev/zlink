# Session Actor dispatch

[공통 스펙 목차](README.ko.md) · [STREAM 서버 session](30-stream-session.ko.md) ·
[Actor 모델](22-actor-model.ko.md) · [Spot과 Actor membership](23-spot-actor.ko.md)

## 1. 이 문서가 정의하는 범위

이 문서는 ZLink Framework 11.0.0에서 STREAM session(연결 하나의 packet 처리와
request correlation을 유지하는 실행 단위)과 Actor runtime을 연결하는
typed dispatch, binding, owner handoff와 실행 순서를 정의한다.

Core raw transport는 Actor identity, 이전 session 작업을 구분하는 binding token,
같은 object incarnation에서 [owner](01-glossary.ko.md#owner)가 바뀐 순서를 나타내는 `AuthorityOwnerGeneration`,
sequence barrier와 Actor route를 해석하지 않는다.

`EnableActorDispatch()`는 `MeshName`을 받지 않고 global object dispatch capability를
활성화한다. Startup은 같은 process에 Object `Client` 또는 `Server` role과 Location
Store가 하나 이상 구성되어 있는지 확인한다.

여러 Mesh가 구성되어 있어도 오류가 아니다. Global ActorId authority가 current
Mesh와 owner를 찾는다. STREAM-only node가 Actor dispatch를 사용하지 않는다면
MeshNode는 필요하지 않다.

Object role이 `None`이거나 Location Store가 없으면 Actor dispatch enablement를 startup에서 거부한다. Hidden
same-process Actor [authority](01-glossary.ko.md#authority)나 local-only binding 의미를 제공하지 않는다.

## 2. Application에 보이는 전체 흐름

Application은 session object, `ActorRef`, typed payload·reply와 bound-session API만
사용한다. Node RID, STREAM transport handle, raw relay envelope, request sequence,
[AuthorityOwnerGeneration](01-glossary.ko.md#authority-owner-generation)과 endpoint를 직접 조립하거나 보관하지 않는다.

1. Session callback이 client를 인증하고 domain Actor identity와 type을 정한다.
2. Global ActorId로 Ready ActorRef를 lookup하거나 application 정책에 따라 Actor를 명시적으로 생성한다.
3. Session과 Actor를 [binding token](01-glossary.ko.md#binding-token)으로 bind한다.
4. Session handler가 typed payload를 current Actor route로 제출한다.
5. Actor handler는 typed reply를 반환하거나 current bound session으로 one-way push를 보낸다.

## 3. Inbound dispatch와 reply

STREAM packet은 먼저 session의 typed handler registry로 dispatch된다. Handler가
Actor dispatch를 선택하면 Framework는 다음 값을 internal envelope에 보존한다.

- 원본 request correlation
- Binding token
- Actor `ObjectGeneration`
- `AuthorityOwnerGeneration`
- `OwnerLeaseGeneration`: current owner host process lifecycle을 구분한다.
- Session sequence: 현재 session에서 수락한 message의 순서를 나타낸다.

Payload는 local·remote 여부와 관계없이 target Actor application queue에 직접 추가한다. Current Spot은 authority
검증에 사용하지만 callback 실행 문맥이 아니다. Session callback thread에서 Actor handler를 실행하지 않으며 서로
다른 Actor를 session 또는 [Spot](01-glossary.ko.md#spot) global queue로 직렬화하지 않는다.

Request reply·error는 original STREAM correlation을 terminal-once로 완료한다. Request를
target Actor route에 제출한 뒤 timeout, cancellation 또는 route failure가 발생하면 target이 이미 업무를 실행했는지
확정하지 못할 수 있다. Framework는 이런 실패 뒤 다른 Actor, 새 owner 또는 다른
[MeshNode](01-glossary.ko.md#meshnode)를 선택해 같은 request를 자동으로 다시 보내지 않는다.

Session이 닫힌 뒤 늦게 도착한 reply도 새 session이나 새 binding의 reply로 사용하지
않는다. 서로 다른 session의 request가 같은 업무 결과를 공유하는 것을 막기 위한
경계다.

## 4. Binding authority

Binding은 다음 값을 연결하는 runtime 관계다.

- Exact `ActorRef`의 `ActorId`와 `ObjectGeneration`
- Current `AuthorityOwnerGeneration`과
  [OwnerLeaseGeneration](01-glossary.ko.md#owner-lease-generation)
- [STREAM session](01-glossary.ko.md#stream-session) identity
- Binding generation과 token. Binding generation은 같은 session owner lifecycle에서
  binding이 교체된 순서를 구분한다.

한 Actor는 동시에 session binding 하나만 가진다. Session 하나에는 여러 Actor를
bind할 수 있다.

Bind는 caller가 제출한 `ActorRef`의 위치로 control request를 한 번 보낸다. Actor와
STREAM session이 서로 다른 MeshNode에 있으면 session owner가
`boundSessionBind(38)` control request를 Actor owner에 보낸다. Actor owner는
Actor `ObjectGeneration`, target `NodeGeneration`(target node process lifecycle을
식별하는 generation)과
`AuthorityOwnerGeneration`을 모두 확인한 뒤
[binding generation](01-glossary.ko.md#binding-generation)을 등록하고 terminal
reply를 한 번만 반환한다.

Session에서 Actor로 들어가는 payload는 등록된 binding generation과
[session sequence](01-glossary.ko.md#session-sequence)를 포함한 `actorSend(24)` record로 Actor owner에 전달한다. Actor가 session에
보내는 push는 `boundSessionSend(36)` record로 session owner에 전달한다. Session
owner는 source Actor `ObjectGeneration`, source `NodeGeneration`,
`AuthorityOwnerGeneration`과 expected binding generation이 모두 current일 때만
실제 STREAM connection에 제출한다.

Source는 bind 전에 Store에서 current route를 미리 조회하지 않는다. Local Actor
instance를 받는 overload도 제공하지 않는다.

Binding identity는 session owner Node RID, 그 node의 lifecycle generation과
owner-local binding generation을 함께 사용한다. Binding generation의 대소 비교는
같은 session owner lifecycle 안에서만 유효하다. 다른 MeshNode가 bind하거나 session
owner가 재시작하면 owner-local counter가 이전 값보다 작더라도 새로운 lifecycle
identity로 등록할 수 있다.

Rebind는 새 identity를 Actor owner와 session owner 양쪽에 등록한 뒤 이전 identity를
무효화한다. Unbind와 disconnect는 `boundSessionBind(38)`의 tombstone transition으로
정확히 해당하는 이전 identity만 제거한다. 이전 owner lifecycle에서 늦게 도착한
push·ingress·close, 이전 Actor `ObjectGeneration`, 이전 authority owner와 재시작 전
`NodeGeneration`은 current binding이나 connection에 적용하지 않는다. 형식이 잘못된
control 및 one-way record는 application queue에 넣지 않으며 one-way record에는 별도
terminal route를 만들지 않는다.

Target에 exact Actor가 없고 active committed forwarding mapping이 있으면 original bind control request와 reply
route를 mapping target으로 relay한다. Mapping이 없거나 만료됐으면 `ActorLocationStale`, 같은 ActorId의
ObjectGeneration이 다르면 `ActorGenerationStale`, relocation pre-commit seal 중이면 `ActorMoving`으로 끝난다.
Source는 Store에서 새 route를 찾아 같은 bind를 hidden retry하지 않는다. `BindOrGet`의 Get은 같은 session의
exact ActorId·[ObjectGeneration](01-glossary.ko.md#objectgeneration) binding만 반환하며 다른 generation이나 directory Actor를 반환하지 않는다.

Binding route는 Framework가 관리한다. Application은 별도 Location row, proxy,
session RID나 endpoint를 만들지 않는다.

Bound-session API는 current binding으로 one-way push를 보내거나 connection close를
요청한다. 임의의 session을 지정하는 global proxy는 제공하지 않는다. Disconnect는
binding을 해제하지만 Actor를 destroy하거나 Spot membership을 바꾸지 않는다.

다음 .NET 발췌는 session이 exact `ActorRef`를 bind하고 payload를 Actor queue로
relay하는 공개 표면을 보여준다. 다른 언어에 같은 signature를 요구하지 않으며,
정확한 .NET 계약은
[.NET STREAM session interface](server/languages/dotnet/interfaces/07-stream-session.ko.md)가
정의한다.

```csharp
public interface IZLinkSessionActors
{
    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActor
{
    ActorRef Ref { get; }
    ValueTask RelayAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}
```

```csharp
var boundActor = await session.Actors
    .BindAsync(actorRef, cancellationToken); // 이 incarnation과 session을 고정한다.

await boundActor.RelayAsync(
    dispatch,
    payload,
    cancellationToken); // 원래 request 정보와 session sequence를 보존해 제출한다.
```

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

```mermaid
sequenceDiagram
    participant Framework
    participant SourceActor as Source Actor
    participant SessionOwner as Session owner
    participant RelocationStore as Relocation Store
    participant LocationStore as Location Store
    participant TargetActor as Target Actor

    Framework->>SourceActor: Actor admission 봉인
    Framework->>SessionOwner: ingress 봉인과 high-water 확인 요청
    SessionOwner-->>Framework: 정확한 high-water ACK 반환
    Framework->>SourceActor: bound-session request를 terminal drain
    Framework->>RelocationStore: Actor state와 허용된 one-way journal 저장
    TargetActor->>RelocationStore: root를 읽어 staging queue와 새 route 준비
    Framework->>Framework: durable source cleanup 완료
    Framework->>LocationStore: authority를 Completed로 CAS
    TargetActor->>SessionOwner: exact generation으로 route 전환 요청
    SessionOwner-->>TargetActor: atomic route 전환 ACK
    Framework->>TargetActor: callback과 accepted journal replay
    Framework->>TargetActor: steady target normalization 뒤 admission 개방
```

이 다이어그램은 relocation commit 뒤 session route를 target Actor로 전환하는 정상
경로다. Physical STREAM connection은 Session owner에 남아 있으며, route 전환 ACK와
steady normalization 전에는 target admission을 열지 않는다.

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
거부하고 accepted callback·reply·cleanup을 [deadline](01-glossary.ko.md#deadline)까지 처리한 뒤 connection을 닫는다. Physical connection을
다른 process로 이동하지 않는다.

## 8. Startup과 operation error

| Condition | Result |
|---|---|
| Object `Client`·`Server` role이 없다. | Configuration error로 startup에 실패한다. |
| [Location Store](01-glossary.ko.md#location-store)가 없다. | Configuration error로 startup에 실패한다. |
| `ActorRef` 위치가 stale하고 forwarding mapping도 없다. | `ActorLocationStale`로 끝난다. |
| `ObjectGeneration`이 다르다. | `ActorGenerationStale`로 끝난다. |
| Actor가 relocation pre-commit seal 상태다. | `ActorMoving`으로 끝난다. |
| 같은 packet key의 handler를 중복 등록했다. | Configuration error로 startup에 실패한다. |
| Actor factory가 없다. | Explicit create error로 끝난다. |
| Current binding 없이 push 또는 close를 요청했다. | Session-not-bound 오류로 끝난다. |
| Actor·owner·binding fence가 stale하다. | Typed stale error로 끝나며 다른 대상으로 fallback하지 않는다. |

## 9. 구현 및 contract test 검증 요구

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
