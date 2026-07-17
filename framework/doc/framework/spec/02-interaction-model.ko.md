# ZLink Framework 상호작용 모델

[스펙 목차](README.ko.md) · [이전: 개요](01-overview.ko.md) ·
[다음: 메시지 모델](03-message-model.ko.md)

## 1. 목적

이 문서는 framework operation의 대상, 완료 의미와 실행 owner를 정의한다. 정확한 언어별 메서드
시그니처는 각 package의 `languages/<lang>/` 문서가 소유한다.

## 2. 공통 모델

| 모델 | 대상 선택 | 호출자가 관찰하는 완료 |
|---|---|---|
| node direct send | 같은 MeshName의 RID 하나 | message submit 결과 |
| node direct request | 같은 MeshName의 RID 하나 | reply, timeout 또는 route 오류 |
| channel send | ChannelName의 ready member 하나 | message submit 결과 |
| channel request | ChannelName의 ready member 하나 | reply, timeout 또는 route 오류 |
| Logical Multicast | ChannelName의 remote member와 local Spot match | publish admission 결과 |
| Spot direct | Spot address의 node와 generation | submit 또는 reply 결과 |
| Actor direct | ActorRef의 node와 generation | submit 또는 reply 결과 |
| classic fanout | 준비된 subscriber 집합 | local publisher transport의 수락 |
| STREAM | session RID로 식별한 연결 | packet submit 또는 session lifecycle event |

## 3. Node direct와 channel select-one

Node direct는 infrastructure와 명시적 owner routing에 사용한다. target RID가 현재 mesh member가 아니면
target-not-found 결과를 내고, member이지만 pipe가 준비되지 않았으면 send readiness 한계까지 기다린 뒤
route-not-connected 결과를 낸다. Node direct operation은 실패한 request를 다른 node에 자동으로 다시
보내지 않는다.
다만 Spot direct request에서 handler가 실행되지 않았음이 명확한 stale-target 응답을 받은 경우에는 같은
논리 Spot의 route를 한 번 갱신하고 한 번 다시 제출할 수 있다. 이 제한된 예외는
[Spot 주소 메시징 §5](server/24-spot-address-messaging.ko.md#5-stale-route)가 소유한다.

Channel operation은 호출 순간의 ready member 가운데 weight가 0보다 큰 하나를 round-robin으로 고른다.
선택과 submit 사이에 application callback을 두지 않는다. weight 0은 새 channel 선택과 Logical Multicast
remote target에서만 제외하며 RID direct와 이미 제출한 operation에는 영향을 주지 않는다.

## 4. Send와 request

`send`는 reply가 없는 one-way operation이다. 반환은 destination handler가 실행되었다는 확인이 아니라
Framework가 message를 submit할 수 있는 상태로 받아들였다는 뜻이다. 반환 뒤 발생한 one-way 오류는
runtime error sink와 monitoring으로 보고한다.

`request`는 reply correlation을 만들고 terminal 결과를 정확히 한 번 전달한다. request timeout은 reply를
기다리는 시간이다. 전송 단계의 backpressure는 send timeout이 담당한다. route 오류나 timeout으로 끝난
request를 Framework가 자동 재전송하지 않는다. Spot direct request의 안전한 stale-target refresh는
handler 미실행을 확인할 수 있을 때 한 번만 허용하며 timeout, cancellation 또는 실행 여부가 불명확한
실패에는 적용하지 않는다. Core result에서 공통 Framework 결과로 가는 exact mapping은
[Framework API §13.1](05-framework-api.ko.md#131-core-result-변환)이 소유한다.

같은 origin이 같은 destination pipe에 성공적으로 submit한 message는 FIFO다. 서로 다른 destination,
origin 또는 session 사이의 전역 순서는 보장하지 않는다.

## 5. Spot Logical Multicast

Logical Multicast publish는 target ChannelName, topic과 typed payload를 받는다. publish 시점에 remote
MeshNode와 local Spot match를 snapshot한다.

- remote MeshNode마다 routed message를 한 번 submit한다.
- 수신 MeshNode가 `(ChannelName, topic filter)`의 local subscription을 검사한다.
- 같은 node의 일치하는 Spot queue는 immutable payload storage의 reference를 공유한다.
- 다른 MeshNode로 relay하거나 과거 event를 replay하지 않는다.

기본 `NoDrop = true`는 모든 snapshot target을 하나의 admission 단위로 처리한다. blocking publish는
send timeout까지 기다리며, timeout이면 어느 target에도 commit하지 않는다. non-blocking publish는 하나라도
막혀 있으면 즉시 backpressure 결과를 반환한다. `NoDrop = false`에서는 막힌 target만 제외하고 나머지
target에 commit할 수 있다.

publish 성공은 Spot handler의 실행 완료를 뜻하지 않는다. 모든 target queue와 pipe에 admission이
commit되었다는 뜻이다.

## 6. Classic fanout

Classic fanout은 MeshNode와 독립된 publisher/subscriber channel이다. 현재 연결과 subscription 준비가
완료된 subscriber에게만 새 event를 전달한다. publisher는 연결 전 또는 연결 단절 중 event를 저장하지
않고, 다시 연결된 뒤 replay하지 않는다.

Logical Multicast와 classic fanout은 모두 publish/subscribe 사용 경험을 제공하지만 전달 대상과 보장이
다르므로 별도 기능으로 등록한다.

## 7. Spot과 Actor

Spot은 MeshNode가 소유하는 logical mailbox다. Spot direct message, Logical Multicast, timer와 Spot
lifecycle callback은 같은 Spot의 application turn에서 직렬로 처리한다. Node callback이 Spot queue를
대신 읽지 않는다.

`ActorRef`는 owner node의 `NodeRid`, 논리 `ActorId`, 현재 `Generation` 세 값으로 구성하는 immutable
value다. endpoint, 내부 frame, location row와 Actor type은 `ActorRef`에 넣지 않는다. Framework가
wire DTO를 제공하는 언어에서는 `ActorRefSnapshot`도 같은 세 값을 보존하며 별도 인자 없이
`ActorRef`로 복원한다.

Actor message는 ActorRef의 generation과 route epoch를 검증한 뒤 Actor mailbox에 직접 들어간다. Actor
payload는 Spot message queue를 경유하지 않는다. Actor handler는 Actor application turn에서만 실행한다.
Spot 소유 상태를 읽거나 바꿔야 하면 명시적인 Spot send/request를 제출하고 해당 Spot turn에서 처리한다.

Node, Spot과 Actor의 completion과 send-ready는 application handler가 대기 중이어도 진행할 수 있는
infrastructure 실행 영역에서 처리한다.

## 8. STREAM session

STREAM session은 연결 lifecycle과 packet 순서를 소유한다. session callback은 transport callback에서
직접 실행하지 않고 Framework가 관리하는 queue를 거친다. 같은 session의 packet과 lifecycle callback은
직렬로 실행하며 서로 다른 session 사이의 전역 순서는 보장하지 않는다.

Session과 Actor가 bind되면 session ingress는 Actor mailbox로 complete message를 submit한다. Actor에서
client로 보내는 message는 현재 binding의 session FIFO를 사용한다. Actor 이동 중에는 session barrier가
old epoch와 new epoch의 순서를 구분한다.

## 9. Handler 실패

reply route를 복원할 수 있는 request는 구조화된 error reply로 완료한다. reply route를 복원할 수 없는
message와 one-way message는 drop하고 원인에 맞는 log, metric과 observer event를 남긴다. application
handler 예외는 one-way 경로에서도 error로 기록한다. observer 실패는 원래 reply 또는 drop 결과를
바꾸지 않는다.

## 10. 종료

drain이 시작되면 새 channel 선택, Logical Multicast target과 새 상태 배정을 제한한다. 이미 admission한
message, request completion, Actor transfer와 STREAM session barrier는 설정된 deadline까지 진행한다.
deadline 뒤 남은 operation은 owner별 terminal shutdown 결과로 완료한다.
