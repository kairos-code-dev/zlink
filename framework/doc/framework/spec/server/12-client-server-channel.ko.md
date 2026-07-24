# ClientServer Channel — 공통 스펙

[스펙 목차](../README.ko.md) · [Channel 메시징](11-channel-messaging.ko.md) ·
[Network listener identity](13-network-listener-identity.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

## 1. 범위

ClientServer Channel은 client가 업무 send 또는 request를 시작하고 server가 handler 실행과 request reply를
담당하는 단방향 service 경계다. 이 문서는 역할, 발견, 대상 선택, drain과 재시작 계약을 소유한다.
Channel 호출자가 사용하는 논리 주소와 완료 의미는 [11 Channel 메시징](11-channel-messaging.ko.md)이
소유한다.

ClientServer Channel은 RouteMesh의 option이 아니다. Node direct, Spot, Actor와 Logical Multicast를
제공하지 않으며, 다른 RouteMesh를 대신 연결하거나 중계하지 않는다.

## 2. Client와 server 역할

한 process는 같은 ChannelName의 ClientServer Channel에 `Client`, `Server` 또는 두 역할을 함께 등록할 수
있다. Registration key는 `(ChannelName, Role)`이며 역할별 등록은 최대 한 번만 허용한다.

| 역할 | 허용되는 동작 |
|---|---|
| Client | ready server 하나를 선택해 send 또는 request를 시작한다 |
| Server | send·request handler를 실행하고, 받은 request의 reply token으로 reply한다 |

Server에는 연결된 client를 대상으로 새 업무 send나 request를 시작하는 공개 기능이 없다. Client가 시작한
request의 reply는 반대 방향 업무 호출이 아니며 같은 request의 terminal completion이다. Client가 제출한
request와 일치하지 않는 server message는 업무 handler에 전달하지 않고 protocol 오류로 관측한다.

같은 ChannelName의 server를 여러 process에 등록할 수 있다. 같은 process에서 Client와 Server를 각각 한 번
등록하면 별도 registration을 유지하면서 하나의 ClientServer topology를 공유한다. 같은 역할을 두 번
등록하거나 같은 ChannelName을
RouteMesh와 ClientServer에 동시에 등록하면 host startup이 실패한다. RouteMesh와 fanout의 기존
ChannelName 충돌 규칙은 바꾸지 않는다.

## 3. Endpoint와 발견

Client는 manual endpoint 또는 location store automatic discovery 중 하나 이상으로 server endpoint를
얻는다. 두 source가 같은 server identity와 lifecycle generation을 가리키면 연결 intent 하나로 합친다.

Automatic discovery server는 [40 Location runtime](40-location-runtime.ko.md)의 ClientServer server
descriptor와 owner lease를 게시한다. Client는 같은 ChannelName의 유효한 descriptor만 읽고, endpoint에
연결한 뒤 transport identity와 lifecycle generation을 확인해야 ready target으로 사용한다. Descriptor가
보인다는 사실만으로 ready로 간주하지 않는다.

Automatic mode에서는 Client만 발견한 server endpoint로 연결을 시작하며 Server는 client endpoint를
찾거나 outbound connect를 시작하지 않는다. Client는 같은 ChannelName의 유효한 server descriptor마다
identity·lifecycle generation으로 구분한 connection intent 하나를 만든다. 여러 server가 발견되면 각
server와의 ready connection을 독립적으로 유지하고, 업무 호출 때 §4의 weight 규칙으로 그중 하나를
선택한다. 이는 RouteMesh의 RID pairwise initiator 규칙을 사용하지 않는 비대칭 topology다.

ClientServer server descriptor는 MeshName, RouteMesh membership, Spot 또는 Actor location을 포함하지
않는다. MeshNode descriptor를 ClientServer discovery에 재사용하지 않으며, ClientServer descriptor를
RouteMesh peer admission에 사용하지 않는다.

Descriptor에는 normalized message size를 저장하지 않는다. 이 값은 실제 physical connection의 socket
option과 transport 조건을 함께 반영한 결과이므로 Client와 Server가 service admission에서 교환하고, 그
connection에만 적용한다. 아직 연결하지 않은 endpoint의 값을 Framework가 임의로 만들거나 Location Store에
게시하지 않는다.

Manual endpoint만 사용하면 location store가 필요하지 않다. Automatic discovery를 활성화했는데 location
store가 없으면 listener bind 전에 startup이 실패한다.

같은 process에 동일한 ChannelName의 Client와 Server를 함께 등록하면 Framework가 bind를 마친 local
Server endpoint를 별도 peer source로 사용한다. 이 local-only 경로에는 location store나 application의
manual endpoint 등록이 필요하지 않다. Location store도 등록한 경우에는 local registration과 discovery
descriptor가 같은 Server RID와 lifecycle generation을 가리키므로 ready target 하나로 합친다.

Manual connection도 Client가 application에 등록된 endpoint로 연결을 시작한다. Transport admission에서
ChannelName, Server RID, lifecycle generation, weight,
drain state와 security identity를 확인한다. 이 값은 ClientServer 연결 control이며 MeshNode descriptor나
RouteMesh peer admission으로 변환하지 않는다. Manual과 automatic source가 같은 Server RID와 generation을
가리키면 ready target 하나로 합친다.

## 4. Weight와 대상 선택

Server weight 범위는 `0..100`이고 기본값은 `100`이다. Positive weight의 ready server는 weight 비율을
반영해 선택하며 같은 weight의 server는 순환 방식으로 선택한다. Weight `0` 또는 draining server는 새
send와 request의 대상에서 제외한다.

같은 process에 등록한 local Server도 remote Server와 같은 candidate 집합에 포함한다. Listener와 service
admission을 마쳐 ready이고, weight가 양수이며, draining 상태가 아닐 때만 선택한다. Local이라는 이유로
우선 선택하거나 remote Server를 제외하지 않는다. 선택된 local Server에도 Client DEALER에서 Server
ROUTER로 실제 transport message를 전달하며 handler를 직접 호출하지 않는다. 이 경로는 codec, HWM,
timeout, cancellation, correlation과 terminal completion 규칙을 우회하지 않는다.

대상 선택과 submit은 한 operation이다. Application에 선택한 server identity를 중간 결과로 반환하지
않는다. Submit 뒤 연결 종료, timeout 또는 cancellation이 발생해도 다른 server로 자동 재전송하지 않는다.
Server가 request를 실행한 뒤 reply만 전달되지 않았을 수 있기 때문이다.

Descriptor의 weight 또는 drain state가 바뀌면 revision을 증가시킨다. Client는 같은 lifecycle generation의
더 큰 revision만 적용하며 낮은 revision으로 ready target 집합을 되돌리지 않는다.

Local Server의 runtime weight 변경은 ChannelName으로 대상을 지정한다. Server RID와 endpoint는 remote
target을 구분하는 관측 identity이며 application이 local weight 변경 대상으로 선택하지 않는다.

## 5. Send, request와 reply

Send는 ready server 하나에 one-way message를 submit하며 reply token을 만들지 않는다. Request는 ready
server 하나를 선택해 correlation을 만들고 reply, error, timeout, cancellation 또는 shutdown 가운데 먼저
확정된 terminal 결과로 정확히 한 번 완료한다.

Server request handler가 받은 reply token은 그 request에만 사용할 수 있으며 한 번 terminal reply를 만든
뒤 다시 사용할 수 없다. Handler 없음, decode 실패 또는 handler 예외에서 reply route를 복원할 수 있으면
구조화된 error reply로 끝낸다. One-way send의 같은 실패는 drop과 runtime 관측으로 끝낸다.

ClientServer handler가 다른 RouteMesh, 다른 ClientServer Channel, Spot 또는 Actor를 호출해도 원래
ClientServer request correlation을 downstream correlation으로 바꾸지 않는다. Handler가 시작한 downstream
request는 별도 correlation을 사용하고 원래 request는 handler가 반환한 reply로 한 번만 완료한다.

## 6. Drain, 재시작과 store 장애

Server drain은 다음 순서를 따른다.

1. Local ready 상태를 닫고 새 업무 admission을 중단한다.
2. Automatic discovery descriptor에 draining state와 더 큰 revision을 게시한다.
3. 이미 수락한 handler와 request reply를 deadline까지 진행한다.
4. Terminal completion 뒤 descriptor와 owner lease를 해제하고 listener를 닫는다.

Manual client에는 같은 drain state를 연결 control로 전달한다. Drain state를 관찰하기 전에 client가 제출한
request는 server가 거부할 수 있으며 이 경우 유한한 rejected 결과로 끝난다.

같은 server identity가 다시 시작하면 lifecycle generation을 증가시킨다. Endpoint가 같거나 달라도 이전
generation의 pipe와 descriptor를 새 target으로 사용하지 않는다. Client는 새 generation을 ready로 만든 뒤
이전 generation의 연결을 제거하며, 늦은 reply를 새 request correlation에 연결하지 않는다.

Location store 장애 중에는 마지막으로 성공한 automatic connection intent를 유지하고 신규 descriptor
추가·제거 계산을 멈춘다. 이미 ready인 연결과 수락한 request는 store 장애만으로 취소하지 않는다. Server가
lease를 갱신하지 못해 fencing deadline에 도달하면 새 업무 admission을 중단한다.

## 7. 검증 요구

- Server에서 client 대상 업무 호출을 시작하는 공개 기능이 없고 unsolicited server message를 client 업무
  handler에 전달하지 않는다.
- 같은 process의 같은 ChannelName에 Client와 Server를 각각 한 번 등록할 수 있고, 같은 역할의 중복 등록과
  RouteMesh 충돌은 startup 오류다.
- 같은 process의 Client와 Server만 등록한 local-only 구성은 location store나 manual endpoint 없이 실제
  transport admission을 거쳐 ready target을 만든다.
- 같은 ChannelName의 여러 server가 weight, weight `0`과 drain state를 반영해 선택된다.
- Local Server도 remote Server와 같은 readiness·weight·drain 규칙으로 선택하며 실제 transport를 거쳐
  handler에 도달한다.
- Automatic discovery가 전용 ClientServer server descriptor를 사용하며 MeshNode descriptor와 섞이지 않는다.
- Automatic과 manual ClientServer 모두 Client만 server endpoint로 connect하고, automatic Client는 같은
  ChannelName의 descriptor마다 connection intent 하나를 유지한다.
- 같은 identity의 재시작에서 새 lifecycle generation만 ready target이 되고 늦은 reply가 새 request를
  완료하지 않는다.
- ClientServer handler가 다른 송신 경로를 호출해도 원래 request completion은 한 번만 발생한다.
- Store 장애가 이미 ready인 연결을 즉시 끊지 않으며 recovery 뒤 최신 revision과 generation으로 수렴한다.
