# Spot service

이 문서는 ZLink Framework 11.0 service runtime의 Entry Spot과 User Spot lifecycle, direct call,
Channel-scoped Logical Multicast, local subscription과 timer 계약을 정의한다. Instance Spot의 owner claim과
activation은 [Instance Spot](06-instance-spot.ko.md)이 정한다.

## 1. 책임과 Spot 종류

Spot은 하나의 MeshNode가 소유하는 logical destination과 sequential application turn이다. Spot은 network
socket, peer connection 또는 remote subscription을 소유하지 않는다. Direct message와 Logical Multicast는
owner MeshNode의 ROUTER를 사용한다.

| 종류 | 책임 |
|---|---|
| Entry Spot | MeshNode에 하나씩 존재하며 Actor 생성과 initial membership lifecycle을 처리함 |
| User Spot | Application이 명시적으로 생성하며 Actor membership, Spot direct handler, subscription과 timer를 처리함 |
| Instance Spot | Actor membership 없이 logical address로 activation되는 Spot. 별도 계약을 따름 |

Actor 업무 payload는 Actor application queue로 직접 전달한다. Spot application queue와 Spot callback을
경유하지 않는다. Actor join·leave와 lifecycle control만 owner Spot turn에서 처리한다.

Classic PUB/SUB는 별도 socket과 subscriber connection을 사용하는 독립 기능이다. Spot Logical Multicast는
RouteMesh ChannelName membership을 사용하며 PUB/SUB socket을 만들지 않는다.

## 2. Identity와 generation

Spot identity는 `MeshName`, Spot routing ID와 Spot generation의 조합이다. Routing ID는 같은 MeshName에서
logical key를 구분하고 generation은 같은 key의 서로 다른 activation을 fence한다. 같은 routing ID로 새 Spot을
만들어도 이전 handle, message, timer와 callback은 새 generation에 적용되지 않는다.

Spot handle은 exact activation과 current owner route snapshot을 보유하는 opaque capability다. Handle을 닫는
것과 logical Spot을 종료하는 것은 구분한다. Runtime은 handle, Actor membership, timer, active turn과 owner
reference가 모두 해제된 뒤 logical Spot resource를 정리한다.

Application은 owner node RID, lifecycle generation, endpoint, lease와 wire address를 직접 조립하지 않는다.
Snapshot과 monitoring은 identity와 current lifecycle state를 immutable 값으로 제공한다.

## 3. 생성, 조회와 종료

Entry Spot은 MeshNode startup에서 구성하며 node routing ID와 연결된 stable identity를 사용한다. Entry Spot
registration, handler와 Actor factory가 준비되기 전 MeshNode를 `Serving` target으로 공개하지 않는다.

User Spot `Create`와 `GetOrCreate`는 caller가 선택한 local MeshNode에서만 실행한다. 다른 MeshNode를 고르거나
remote create operation을 전달하지 않는다. Local concurrent `GetOrCreate`는 generation 하나와 factory 실행
하나로 수렴한다. Host-level Spot manager의 create, lookup, list와 close는 MeshName을 명시해 local MeshNode를
선택한다.

Store-backed User Spot은 internal `Creating → Ready` publication barrier를 사용한다. NewObject CAS가 final
ObjectGeneration과 AuthorityOwnerGeneration을 발급하고 `Creating` row를 만든다. Factory, configure와 initialize가
모두 성공한 뒤 same object·owner fence의 Ready CAS를 수행한다. Resolver와 remote messaging은 Ready만
사용한다. Entry Spot은 startup initialization이 끝나기 전 descriptor·resolve에 공개하지 않는다.

Factory·configure·initialize 또는 Ready commit이 실패하면 local barrier를 failed·sealed로 고정하고 exact
StoreVersion, ObjectGeneration, AuthorityOwnerGeneration과 owner lease로 row를 fenced delete한다. Ambiguous result는
exact Read로 reconcile한다. Missing을 확인할 때까지 같은 failure를 반환하고 callback을 재실행하지
않으며, 그 다음 caller만 NewObject claim으로 새 generation을 발급한다.

Lookup은 현재 local activation만 반환한다. Remote owner resolve는 Location Store에 게시된 existing owner만
찾으며 존재하지 않는 Spot을 생성하지 않는다. Handle이 가리키는 generation이 close되면 이후 호출은 stale
또는 closed 결과로 끝나고 새 generation으로 자동 변경하지 않는다.

Spot close는 신규 application admission을 seal하고 seal 전에 수락한 turn, timer와 lifecycle callback을
deadline까지 처리한다. Handler scope와 timer를 정리한 뒤 current owner·generation과 일치하는 location을
release한다. Repeated close와 close·timeout·callback 경쟁은 terminal cleanup 하나로 수렴한다.

User Spot에 current Actor membership이 하나라도 있으면 normal Close는 public `false`로 실패하고
admission, authority와 membership을 유지한다. Internal observer는 `InUse/Conflict` reason을 기록할 수 있다.
Caller는 explicit leave·destroy를 끝낸 뒤 close하며 Framework는 Actor를 숨겨서 이동·파괴하지 않는다.

## 4. Direct와 Channel 호출

Spot direct call은 exact Spot activation 또는 logical address가 확인한 current activation을 대상으로 한다.
Local과 remote Spot은 같은 typed handler, metadata, application turn과 completion 의미를 제공한다. Caller는
target endpoint와 route frame을 전달하지 않는다.

Spot handle을 사용하는 direct call은 handle의 immutable Spot generation을 유지한다. Target owner가 다른
generation으로 바뀌면 stale target으로 끝나며 다른 generation에 자동 재제출하지 않는다. Request handler는
reply route를 Framework context에서 사용하고 source route를 재구성하지 않는다.

Spot handler와 timer에서 시작한 Channel call은 ChannelName으로 process-local 송신 경로 하나를 고른다. 그
ChannelName이 다른 RouteMesh 또는 ClientServer를 가리켜도 원래 Spot activation이 request correlation을
소유한다. `Async` completion은 원래 turn을 유지하고 `Yield` completion은 원래 Spot queue의 새 turn으로
재개한다. Reply를 Spot application packet으로 다시 dispatch하지 않는다.

One-way call은 source outbound admission까지만 기다린다. Remote Spot handler 실행 완료를 기다리지 않으며
timeout, cancellation, disconnect와 shutdown 뒤 자동 replay하지 않는다.

## 5. Channel-scoped Logical Multicast

Logical Multicast는 `(ChannelName, topic)`으로 target 범위를 정한다. Runtime은 target ChannelName의 ready
remote MeshNode마다 routed message를 한 번 제출한다. Origin MeshNode도 해당 Server membership에 참여하면
node-local subscription을 검사한다. 각 수신 MeshNode는 자기 local subscription만 검사한다.

한 node의 여러 Spot이 같은 publish를 수신할 때 runtime은 immutable message storage를 공유하고 각 queue가
안전한 reference를 보유한다. Payload를 target 수만큼 다시 encode하거나 application에 reference count를
노출하지 않는다.

Publish는 target snapshot 전체를 reserve하는 all-or-none transaction이 아니다. Bounded executor가
transaction을 시작한 뒤 target마다 한 번 제출하고 결과를 집계한다. 앞 target이 수락한 message는 뒤 target의
capacity failure로 취소하지 않는다. Remote transport admission은 수신 node의 local Spot queue 수락을
보장하지 않는다.

Result detail은 remote target의 snapshot·admitted·dropped·unreachable 수와 local Spot의
snapshot·admitted·dropped 수를 구분한다. Remote target 하나 이상이 capacity 때문에 수락하지 못하면
top-level result는 `Backpressured`다. Snapshot target이 모두 0이면 `TargetNotFound`다. Connection 불가와 local
queue drop은 detail에 기록하며 durable 저장, acknowledgement와 replay를 제공하지 않는다.

같은 origin에서 같은 destination으로 수락된 publish는 destination별 FIFO를 유지한다. 서로 다른 origin,
destination과 Spot 사이의 global order는 보장하지 않는다.

## 6. Subscription과 dispatch

Subscription key는 ChannelName, topic filter, match kind와 Spot generation으로 구성한다. Exact match는 topic
전체가 같은 경우, prefix match는 topic이 filter bytes로 시작하는 경우에 성립한다. Empty prefix는 해당
ChannelName의 모든 topic과 일치한다.

중복 subscription 등록과 존재하지 않는 subscription 해제는 idempotent하다. 등록 또는 해제가 완료된 뒤
시작한 publish는 새 snapshot을 사용한다. Concurrent publish는 변경 전 또는 변경 후 snapshot 하나만 보고
partial registration이나 duplicate delivery를 만들지 않는다.

Subscription은 node-local이며 remote peer에 Spot 목록이나 filter inventory를 게시하지 않는다. Spot이
종료되거나 generation이 바뀌면 이전 subscription은 local match에서 제외한다.

Spot application queue에는 direct payload, matching multicast payload, timer와 Actor membership control만
추가한다. Application callback은 Spot turn에서 순차 실행한다. Infrastructure completion과 transfer progress는
application turn과 독립적으로 진행한다.

## 7. Timer와 lifecycle fence

Spot timer는 하나의 Spot generation과 handler scope에 속한다. 같은 Spot의 timer callback과 다른 application
callback은 동시에 실행하지 않는다. `Yield`로 turn을 반납한 경우에만 다음 application record가 먼저 실행될
수 있다.

Timer stop 또는 close의 terminal completion 뒤 새 tick을 전달하지 않는다. Spot close, generation 변경,
owner lease 만료와 maintenance transfer는 이전 owner의 pending tick을 generation·authority fence로 거부한다.
Runtime timer handle과 callback continuation은 다른 owner로 이동하지 않는다. `Snapshot` transfer가 timer
업무 상태를 필요로 하면 application state에 다음 실행 시각과 반복 정보를 포함하고 target restore 뒤 새
timer를 등록한다.

## 8. Limit, 동시성과 종료

Spot queue는 owner MeshNode의 유한 message·byte budget을 사용한다. Logical Multicast local admission은
capacity가 없으면 기다리지 않고 target별 drop으로 기록한다. Direct one-way admission은 source MeshNode의
send timeout과 bounded waiter limit을 사용한다.

Send, request, publish, subscription query와 immutable status는 concurrent call에 안전하다. Create, close,
membership mutation과 maintenance transfer는 같은 Spot authority에서 직렬화한다. Callback 안에서 같은 Spot의
destructive close를 동기적으로 기다리면 deadlock 오류다.

Host `Shutdown`은 새 Spot과 handler admission을 차단하고 accepted turn을 deadline까지 처리한 뒤 local Spot을
정리한다. Host `Retire`는 User Spot instance가 하나라도 남아 있으면 state 유무를 추론하지 않고
`Blocked/TransferDisabled`로 끝난다. Entry Spot은 transfer하지 않으며 target MeshNode startup에서 구성한다.

## 9. 검증 요구

- Entry Spot과 User Spot의 create·lookup이 local-only 계약을 지킨다.
- Store-backed User Spot이 Creating에서 remote Ready로 관측되지 않고 creation failure가 exact delete로 수렴한다.
- Current Actor membership이 있는 User Spot Close가 `false`로 실패하고 state를 변경하지 않는다.
- Close된 generation의 handle, callback, timer와 subscription이 새 generation에 적용되지 않는다.
- Actor 업무 payload가 Spot application queue와 Spot callback을 거치지 않는다.
- Spot Channel call이 process-local 송신 경로를 사용하고 원래 Spot completion owner를 보존한다.
- Logical Multicast가 remote node마다 한 번 전송되고 수신 node가 local subscription만 검사한다.
- Partial publish에서 먼저 수락한 target이 뒤 target failure로 취소되지 않는다.
- Subscription 변경과 concurrent publish가 complete snapshot 하나만 사용한다.
- Timer와 Spot callback이 한 owner turn에서 직렬화되고 stale tick이 거부된다.
- User Spot이 남은 `Retire`가 application admission을 바꾸지 않고 preflight에서 차단된다.
