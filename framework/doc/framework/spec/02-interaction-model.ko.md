# ZLink Framework 상호작용 모델

[스펙 목차](README.ko.md) · [이전: 개요](01-overview.ko.md) ·
[다음: 메시지 모델](03-message-model.ko.md)

## 1. 목적

이 문서는 ZLink Framework 11.0.0 operation의 대상, 완료 의미와 실행 owner를 정의한다. 정확한 언어별 메서드
시그니처는 각 package의 `languages/<lang>/` 문서가 소유한다.

## 2. 공통 모델

| 모델 | 대상 선택 | 호출자가 관찰하는 완료 |
|---|---|---|
| node direct send | 같은 MeshName의 RID 하나 | message submit 결과 |
| node direct request | 같은 MeshName의 RID 하나 | reply, timeout 또는 route 오류 |
| channel send | ChannelName에 등록된 RouteMesh 또는 ClientServer 송신 경로의 ready target 하나 | message submit 결과 |
| channel request | ChannelName에 등록된 RouteMesh 또는 ClientServer 송신 경로의 ready target 하나 | reply, timeout 또는 route 오류 |
| Logical Multicast | ChannelName의 remote member와 local Spot match | publish admission 결과 |
| Spot direct | Spot address의 node와 generation | submit 또는 reply 결과 |
| Instance Spot direct | MeshName·Instance type·Spot RID의 logical address | resolve·activation을 포함한 submit 또는 reply 결과 |
| Actor direct | ActorRef의 node와 generation | submit 또는 reply 결과 |
| classic fanout | 준비된 subscriber 집합 | local publisher transport의 수락 |
| STREAM | session RID로 식별한 연결 | packet submit 또는 session lifecycle event |

## 3. Node direct와 channel select-one

Node direct는 infrastructure와 명시적 owner routing에 사용한다. target RID가 현재 mesh member가 아니면
target-not-found 결과를 내고, member이지만 pipe가 준비되지 않았으면 send readiness 한계까지 기다린 뒤
route-not-connected 결과를 낸다. Node direct operation은 실패한 request를 다른 node에 자동으로 다시
보내지 않는다.
다만 SpotHandle direct request에서 handler가 실행되지 않았음이 명확한 stale-target 응답을 받은 경우에는 같은
논리 Spot의 route를 한 번 갱신하고 한 번 다시 제출할 수 있다. 이 제한된 예외는
[Spot 주소 메시징 §5](server/24-spot-address-messaging.ko.md)이 소유한다.

Channel operation은 ChannelName으로 process-local 송신 경로를 먼저 결정한다. RouteMesh 경로는 호출 순간의
ready member 가운데 weight가 0보다 큰 하나를 고르고, ClientServer 경로는 ready server 가운데 하나를
고른다. 선택과 submit 사이에 application callback을 두지 않는다. Weight 0은 새 channel 선택에서
제외하며, RouteMesh에서는 Logical Multicast remote target에서도 제외한다. RID direct와 이미 제출한
operation에는 영향을 주지 않는다.

Select-one의 non-blocking submit이 capacity 부족으로 수락되지 않은 경우, 그 첫 선택은 public
target commitment가 아니다. Framework service runtime은 send-ready 이후 성공한 admission 전까지 같은 ChannelName의 현재
eligible member 가운데 다른 target을 선택할 수 있다. Target은 transport queue가 operation을 수락한 시점에
확정되며, 그 이후에는 같은 operation을 다른 member에게 replay하지 않는다. Direct call은 이 재선택
규칙을 사용하지 않는다. Node direct는 RID, Spot·Actor는 handle의 owner와 generation, session은 binding
token을 유지하며 물리 peer lifecycle generation을 public target identity로 노출하지 않는다.

같은 ChannelName을 여러 물리 송신 경로에 등록할 수 없으므로 호출자는 MeshName이나 ClientServer 종류를
지정하지 않는다. 같은 process에서 ChannelName을 서로 다른 topology에 등록하면 host startup이
설정 오류로 실패한다.

Node direct는 MeshName·RID를 계속 사용한다. Logical Multicast 호출자는 ChannelName과 topic만 지정하며
process-local channel index가 owner RouteMesh의 MeshNode를 결정한다. 선택된 owner MeshName은 내부
routing과 runtime monitoring에서만 관측한다.

## 4. Send와 request

`send`는 reply가 없는 one-way operation이다. Public call은 비동기 submit 하나만 제공하며, 즉시 한 번만
시도하는 동기 terminator는 제공하지 않는다. 반환은 destination handler가 실행되었다는 확인이 아니라
Framework가 message를 local outbound queue에 받아들였는지를 나타낸다. Queue가 일시적으로 가득 차면
유한한 send timeout까지 admission을 기다린다. 이미 수락한 뒤 발생한 one-way 오류는 runtime error sink와
monitoring으로 보고한다.

InstanceSpotAddress send도 같은 비동기 terminator를 사용한다. Cold route는 source에서 Location Store
resolve와 eligible target 선택을 거친 뒤 local outbound admission으로 submit을 완료한다. Target-side
claim과 activation은 그 뒤 수행하며 remote receipt가 없으므로 submit 완료 조건이 아니다. Cache hit도
같은 public 의미를 유지하므로 cache 상태에 따라 동기 submit을 제공하거나 caller에게 owner node와
generation을 요구하지 않는다. Local outbound admission 뒤 remote activation이 실패한 one-way message는
다른 owner에게 replay하지 않고 drop 관측으로 남긴다.

유효한 one-way call은 `Submitted`, `Backpressured`, `TimedOut`, `TargetNotFound`, `RouteNotConnected`,
`Shutdown` 가운데 하나로 완료한다. 잘못된 argument·handle·state와 중복 submit은 local exceptional
completion이다. Cancellation은 별도 status가 아니며 언어별 cancelled awaitable로 표현한다. 어느 terminal
결과 뒤에도 Framework가 operation을 자동으로 다시 제출하지 않는다.

`request`는 선택한 송신 경로에 reply correlation을 만들고 terminal 결과를 정확히 한 번 전달한다. request timeout은 reply를
기다리는 시간이다. 전송 단계의 backpressure는 send timeout이 담당한다. route 오류나 timeout으로 끝난
request를 Framework가 자동 재전송하지 않는다. Spot direct request의 안전한 stale-target refresh는
handler 미실행을 확인할 수 있을 때 한 번만 허용하며 timeout, cancellation 또는 실행 여부가 불명확한
실패에는 적용하지 않는다. 언어별 transport 오류는 이 문서의 닫힌 Framework 결과 가운데 하나로 변환하며
transport 전용 결과를 public call에 노출하지 않는다.

InstanceSpotAddress request는 CAS loser가 remote `Ready` owner를 확인한 application pre-admission 단계에서만
한 번 redirect할 수 있다. Target queue가 수락한 뒤의 실패, 수락 여부가 불명확한 연결 오류와 timeout에는
다른 owner로 다시 제출하지 않는다. 첫 계약은 remote admission receipt와 자동 request 재제출을 제공하지
않는다.

다른 RouteMesh 또는 ClientServer Channel로 보낸 request도 같은 단일 terminal completion 규칙을 따른다.
Spot에서 시작한 경우 Framework는 원래 Spot activation과 generation을 completion record에 보존하고, reply를
새 application message로 다시 dispatch하지 않는다.

같은 origin이 같은 destination pipe에 성공적으로 submit한 message는 FIFO다. 서로 다른 destination,
origin 또는 session 사이의 전역 순서는 보장하지 않는다.

## 5. Spot Logical Multicast

Logical Multicast publish는 target ChannelName, topic과 typed payload를 받는다. publish 시점에 remote
MeshNode와 local Spot match를 snapshot한다.

- remote MeshNode마다 routed message를 한 번 submit한다.
- 수신 MeshNode가 `(ChannelName, topic filter)`의 local subscription을 검사한다.
- 같은 node의 일치하는 Spot queue는 immutable payload storage의 reference를 공유한다.
- 다른 MeshNode로 relay하거나 과거 event를 replay하지 않는다.

Framework service runtime은 대기 queue가 없는 bounded I/O executor에 publish transaction을 direct handoff한다.
즉시 사용할 worker slot이 없으면 transaction을 시작하지 않고 `Backpressured`로 완료한다. Handoff에 성공해
transaction이 시작되면 각 remote target을 해당 send timeout까지 제출하고 local Spot queue는 즉시 판단한다.
Transaction 시작이 snapshot operation의 commit point이므로 cancellation이나 shutdown으로 남은 target 제출을
중단하지 않는다.
앞에서 수락된 remote target과 local Spot queue는 뒤 target의 실패 때문에 취소되지 않는다.

Executor direct handoff가 성공하지 못했거나 remote target 하나 이상이 용량 때문에 실패하면
`Backpressured`다. Snapshot target이 모두 0이면 `TargetNotFound`다. Target별 send timeout 뒤의 용량 실패를
`TimedOut`으로 다시 분류하지 않는다. 그 밖의 remote 연결 불가와 local Spot queue drop은 top-level status를
바꾸지 않고 publish detail에 기록한다. Remote target이 모두 연결 불가여서 admitted count가 0이어도
remote capacity drop이 없으면 `Submitted`다.

publish 성공은 Spot handler의 실행 완료를 뜻하지 않는다. snapshot target에 대한 제출 결과가 집계되었다는
뜻이며, remote ROUTER가 수락한 뒤 수신 MeshNode의 local Spot queue에서 발생한 drop까지 보장하지 않는다.

## 6. Classic fanout

Classic fanout은 MeshNode와 독립된 publisher/subscriber channel이다. 현재 연결과 subscription 준비가
완료된 subscriber에게만 새 event를 전달한다. publisher는 연결 전 또는 연결 단절 중 event를 저장하지
않고, 다시 연결된 뒤 replay하지 않는다.

Publisher call은 publisher socket send timeout까지 local admission을 기다리는 비동기 terminator 하나만
제공한다. Subscriber가 0이어도 local publisher queue가 event를 수락하면 `Submitted`다. 이 결과는 subscriber
수신이나 handler 완료를 뜻하지 않는다.

Publish의 공통 입력은 ChannelName, topic과 typed event다. Typed event의 packet name을 topic으로
사용하는 편의 호출도 같은 operation을 만든다. 두 호출은 같은 publisher transport, timeout과
submit 결과를 사용하며 subscriber dispatch는 packet name으로 handler를 선택하고 topic을 handler
context에 보존한다.

Publisher는 전용 location descriptor에 ChannelName과 실제 endpoint를 게시한다. Automatic subscriber는
같은 ChannelName의 live publisher를 모두 연결하고 다른 ChannelName이나 다른 descriptor kind는 연결하지
않는다. Manual subscriber는 명시한 endpoint만 연결한다.

Logical Multicast와 classic fanout은 모두 publish/subscribe 사용 경험을 제공하지만 전달 대상과 보장이
다르므로 별도 기능으로 등록한다.

## 7. Spot과 Actor

Spot은 MeshNode가 소유하는 logical mailbox다. Spot direct message, Logical Multicast, timer와 Spot
lifecycle callback은 같은 Spot의 application turn에서 직렬로 처리한다. Node callback이 Spot queue를
대신 읽지 않는다.

Instance Spot은 Actor membership이 없는 Spot kind다. 등록된 InstanceSpotAddress의 첫 direct call이 location
owner claim을 시작할 수 있으며, factory와 initialize가 성공하고 location이 `Ready`로 commit된 뒤 Framework
activation barrier가 열려야 업무 handler가 실행된다. 같은 address에 동시에 도착한 첫 호출은 Location Store
CAS가 정한 owner 하나와 service runtime의 activation ownership을 통해 factory 실행 하나로 수렴한다. Entry·Domain Spot의 local-only
Create/GetOrCreate와 기존 SpotHandle의 existing-only 의미는 바꾸지 않는다.

`ActorRef`는 owner node의 `NodeRid`, 논리 `ActorId`, 현재 `Generation` 세 값으로 구성하는 immutable
value다. endpoint, 내부 frame, location row와 Actor type은 `ActorRef`에 넣지 않는다. Framework가
wire DTO를 제공하는 언어에서는 `ActorRefSnapshot`도 같은 세 값을 보존하며 별도 인자 없이
`ActorRef`로 복원한다.

Actor message는 ActorRef의 generation과 route epoch를 검증한 뒤 Actor mailbox에 직접 추가한다. Actor
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

Server package의 bound session send, session Actor relay와 명시적인 STREAM send·reply도 같은 async-only one-way admission
결과를 반환한다. 별도 stream connector package의 send builder는 connector package 계약을 따른다. STREAM
reply는 해당 STREAM socket의 send timeout을 사용하며 caller request timeout을 reply admission deadline으로
사용하지 않는다. Reply sequence 또는 one-shot token이 유효하지 않거나 같은 reply call을 두 번 제출하면
local exceptional completion으로 끝난다. 유효한 첫 reply terminator는 transport admission 전에 token을 원자적으로
소비한다. 이 terminator가 backpressure, timeout 또는 cancellation으로 완료되어도 token은 재사용하지
않는다. 같은 token에서 만든 두 call이 경쟁하면 하나만 transport admission을 시작한다.

## 9. Handler 실패

reply route를 복원할 수 있는 request는 구조화된 error reply로 완료한다. reply route를 복원할 수 없는
message와 one-way message는 drop하고 원인에 맞는 log, metric과 observer event를 남긴다. application
handler 예외는 one-way 경로에서도 error로 기록한다. observer 실패는 원래 reply 또는 drop 결과를
바꾸지 않는다.

## 10. 종료

`Retire` 또는 `Shutdown`이 admission seal을 시작하면 새 channel 선택, Logical Multicast target과 새 상태 배정을
제한한다. 이미 admission한 message, request completion, Actor transfer와 STREAM session barrier는 설정된
deadline까지 진행한다. Deadline 뒤 남은 operation은 owner별 terminal shutdown 결과로 완료한다.

Draining MeshNode는 새 Instance placement 후보에서 제외된다. `Shutdown`은 기존 Instance Spot을 다른 node로
이동시키지 않고 수락된 turn을 deadline까지 처리한 뒤 정리한다. `Retire`는 type별 maintenance policy와
authority transaction이 허용한 기존 owner만 target에 materialize한다. 두 operation 모두 Framework admission
seal과 current location authority를 검증하며 stale owner가 `Closing`이나 release를 적용하지 못하게 한다.
