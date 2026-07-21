# MeshNode와 channel topology

이 문서는 ZLink Framework 11.0 service runtime에서 MeshNode가 물리 RouteMesh, 논리 ChannelName
membership, listener identity와 target readiness를 어떻게 소유하는지 정의한다. 언어별 public signature는
각 언어 exact interface 문서가 정한다.

## 1. 책임과 불변 조건

MeshNode는 한 RouteMesh에서 message transport와 server-side service owner를 결합하는 Framework runtime
객체다. MeshNode 하나는 `MeshName` 하나, routing ID 하나와 peer가 연결할 ROUTER listener 하나를 가진다.
Spot, Actor와 STREAM session은 MeshNode의 transport와 infrastructure progress를 사용하지만 별도 peer mesh를
만들지 않는다.

`MeshName`은 물리 연결망과 routing ID namespace를 구분한다. 같은 process에는 같은 `MeshName`의
MeshNode를 하나만 등록할 수 있다. 서로 다른 이름의 MeshNode는 여러 개 등록할 수 있으며 mesh 사이의
자동 relay는 없다.

`ChannelName`은 application이 송신 경로와 handler namespace를 고르는 process-local 논리 주소다. 같은
process에서 하나의 ChannelName은 RouteMesh 또는 ClientServer 중 물리 송신 경로 하나에만 대응한다. 같은
이름을 둘 이상의 물리 topology에 등록하면 startup이 실패한다. Process 밖의 같은 Server ChannelName은
여러 node가 함께 제공할 수 있다.

## 2. Identity와 lifecycle

MeshNode identity는 다음 값을 포함한다.

| 값 | 계약 |
|---|---|
| MeshName | 물리 RouteMesh와 RID namespace를 구분하는 immutable 이름 |
| Routing ID | 같은 MeshName에서 node를 식별하는 immutable 값 |
| Listener endpoint | Peer가 연결할 실제 ROUTER endpoint |
| Lifecycle generation | 같은 RID로 시작한 서로 다른 node lifecycle을 구분하는 단조 증가 값 |
| Descriptor revision | 같은 lifecycle의 mutable descriptor snapshot을 구분하는 단조 증가 값 |
| Server ChannelName set | Startup 전에 확정한 0개 이상의 immutable membership |
| Type capability | Startup 전에 등록한 Actor·Spot·Instance Spot type의 immutable 집합 |

MeshNode는 Server membership 없이 시작할 수 있다. 이 구성은 Node direct와 outbound Channel 호출만
시작하는 process에 사용하며 가짜 ChannelName이나 weight 0 membership을 요구하지 않는다.

Framework host는 registration, listener bind, provider 검증과 recovery를 마친 뒤 MeshNode를 target
selection에 공개한다. Host가 `Draining`, `Stopped` 또는 `Error`이면 새 application target으로 사용하지
않는다. Component 상태와 host의 `FrameworkRuntimeState`는 서로 다른 관측 값이며 이름이나 숫자를 합치지
않는다.

## 3. Listener identity

Local bind 주소와 remote process에 게시하는 접속 주소를 구분한다. `BindHost`는 local interface를,
`AdvertiseHost`는 peer가 실제로 연결할 주소를 뜻한다. Wildcard host는 bind에 사용할 수 있지만 advertised
endpoint에는 사용할 수 없다.

Automatic discovery listener는 port를 생략하면 port 0으로 bind한 뒤 실제 port를 읽어 descriptor에
게시한다. Advertised endpoint는 `AdvertiseHost`와 이 실제 port의 조합이다. Wildcard host, port 0 또는
remote에서 접속할 수 없는 주소가 descriptor에 남으면 startup이 실패한다.

RouteMesh, ClientServer server, classic fanout publisher와 STREAM listener는 같은 process 기본 network
identity를 사용할 수 있지만 각 listener의 endpoint와 lifecycle generation은 독립적이다. 각 endpoint는
자기 topology의 descriptor에만 기록한다. Spot·Actor location row에 listener endpoint를 복제하지 않는다.

## 4. ChannelName role과 membership

RouteMesh Channel은 `Client` 또는 `Server` 역할로 등록한다.

| 역할 | Runtime 등록 결과 |
|---|---|
| Client | Process-local 송신 경로만 등록하며 peer descriptor의 target membership에는 포함하지 않음 |
| Server | 송신 경로와 target membership을 등록하고 handler namespace와 weight를 제공함 |

Server는 같은 ChannelName으로 outbound 호출도 시작할 수 있으므로 Client를 중복 등록하지 않는다.
Weight 0은 Server를 새 select-one과 Logical Multicast target에서 제외하는 값이며 Client 역할을 뜻하지
않는다.

Role과 Server ChannelName set은 startup 뒤 바꿀 수 없다. Weight는 `0..100`이고 기본값은 100이다. 실행 중
weight를 바꾸면 descriptor revision을 증가시키고 전체 weight snapshot을 admitted peer와 Location Store에
같은 의미로 게시한다. 같은 lifecycle의 더 큰 revision만 적용한다. Weight 변경은 connection이나 lifecycle
generation을 다시 만들지 않으며 이미 수락한 operation과 RID direct traffic에 영향을 주지 않는다.

## 5. Discovery와 peer admission

Framework는 manual endpoint 또는 Location Store descriptor에서 peer connection intent를 만든다. 두 방식은
같은 handshake, identity 검증과 message path를 사용한다. Manual mode에서 expected RID를 지정하면 handshake
결과가 반드시 일치해야 한다.

Peer admission은 MeshName, RID, lifecycle generation, descriptor revision, immutable ChannelName set,
channel weight, protocol capability와 security identity를 검증한다. MeshName이나 trust identity가 다르거나
같은 generation의 RID가 중복되면 ready로 만들지 않는다. 같은 RID의 더 큰 lifecycle generation이
admission되면 이전 generation을 새 target에서 제외하고 accepted work를 정리한 뒤 connection을 교체한다.

양쪽에서 동시에 연결을 시도해도 같은 RID와 lifecycle generation은 ready connection 하나로 수렴한다.
Application은 initiator, duplicate pipe와 connection ownership을 설정하지 않는다. Manual과 automatic source가
같은 endpoint를 가리키면 하나의 connection intent로 합치며 source 하나가 사라져도 다른 source가 유효하면
connection을 유지한다.

같은 MeshName의 ready MeshNode는 직접 연결된 full mesh를 구성한다. ChannelName 수는 physical peer connection
수를 늘리지 않는다. Peer 하나의 failure는 다른 peer나 host 전체 상태를 `Error`로 바꾸지 않는다.

## 6. Readiness와 target 선택

Connection은 transport 연결, service handshake, identity·generation 검증과 local handler readiness가 모두
끝난 뒤 ready가 된다. Descriptor가 존재하거나 connect 요청이 수락됐다는 사실만으로 ready가 되지 않는다.

Channel select-one은 process-local ChannelName route를 먼저 찾은 뒤 해당 topology의 positive-weight
`Serving` target 하나를 선택하고 같은 operation에서 submit한다. RouteMesh는 같은 MeshName의 Server
membership을, ClientServer는 같은 ChannelName의 ready server를 사용한다. 선택한 RID를 application에
중간 결과로 반환하지 않는다.

Local MeshNode도 해당 Server membership이 있고 host가 `Serving`이며 weight가 양수이면 RouteMesh Channel
target이 될 수 있다. Local target과 remote target은 같은 selection cursor와 application admission 의미를
사용한다.

`Draining` target과 weight 0 membership은 새 Channel select-one, Logical Multicast와 Instance placement에서
제외한다. RID direct는 caller가 지정한 identity를 유지하지만 target의 application admission seal은 새
payload를 종료 또는 moving 결과로 거부할 수 있다.

## 7. Topology별 messaging 경계

| Topology | Physical transport | 대상 의미 |
|---|---|---|
| RouteMesh Node direct | MeshNode ROUTER | 같은 MeshName의 exact RID |
| RouteMesh Channel | 같은 MeshNode ROUTER | ChannelName의 ready Server member 하나 |
| Spot Logical Multicast | 같은 MeshNode ROUTER | ChannelName의 ready member 전체와 node-local Spot match |
| ClientServer | Client DEALER와 Server ROUTER | 한 ChannelName의 ready server 하나 |
| Classic fanout | 별도 PUB/SUB socket | 같은 ChannelName의 connected subscriber |
| STREAM server | 별도 STREAM listener | Connection별 packet session |

RouteMesh ChannelName마다 socket이나 endpoint를 만들지 않는다. Spot direct, Actor direct와 Logical Multicast도
MeshNode ROUTER를 공유한다. Classic fanout subscriber는 fanout publisher descriptor만 조회하며 RouteMesh 또는
ClientServer descriptor를 연결 대상으로 사용하지 않는다.

ChannelName이 다른 RouteMesh나 ClientServer 송신 경로를 가리키더라도 Spot에서 시작한 request correlation은
원래 Spot activation이 소유한다. Runtime은 다른 mesh의 application queue에 reply packet을 중계하지 않는다.

## 8. Listener와 runtime limit

Transport 설정은 startup 전에 확정한다. 실행 중에는 Channel weight만 바꿀 수 있다. Maximum inbound message
size는 complete transport message에 적용하며 상한을 넘긴 payload의 일부를 handler에 전달하지 않는다.

MeshNode는 transport HWM과 send timeout, owner mailbox의 message·byte budget을 함께 적용한다. Mailbox는
두 budget 가운데 먼저 도달한 한도에서 새 admission을 기다리게 하거나 backpressure한다. 기본값은 유한해야
하며 unbounded queue를 사용하지 않는다. Worker 수, queue storage와 transport별 retry 순서는 application
option으로 노출하지 않는다.

Listener TLS, routing identity와 transport option은 해당 topology가 지원하는 범위에서 startup 전에
설정한다. Raw ROUTER·DEALER·PUB option을 MeshNode나 Spot public API에 그대로 투영하지 않는다.

## 9. 종료와 resource ownership

Host `Retire`와 `Shutdown`은 MeshNode, ClientServer server, fanout publisher와 STREAM listener를 host 단위로
조정한다. Admission seal 뒤에도 accepted reply, transfer control과 STREAM barrier에 필요한 connection은
deadline까지 유지한다. Descriptor를 `Draining`으로 게시했다는 이유만으로 connection을 즉시 닫지 않는다.

종료 순서는 application handler와 scope, owner authority, descriptor lease, peer/listener, executor와 raw
transport 순이다. Runtime이 만든 reconnect timer, service liveness timer, monitor subscription과 pending callback은
terminal cleanup 뒤 남지 않아야 한다.

## 10. 검증 요구

- 같은 process의 중복 MeshName과 서로 다른 physical route의 중복 ChannelName이 startup에서 실패한다.
- Server membership이 0개인 MeshNode가 Node direct와 outbound Channel 호출을 수행한다.
- 한 MeshNode의 여러 ChannelName이 같은 peer connection을 사용한다.
- Manual과 automatic peer가 같은 admission과 duplicate-connection 규칙을 따른다.
- Client 역할은 descriptor membership에 나타나지 않고 Server 역할만 weight를 게시한다.
- Weight revision과 더 큰 lifecycle generation이 stale snapshot과 connection을 되살리지 않는다.
- Wildcard host와 port 0이 advertised endpoint에 남지 않는다.
- RouteMesh, ClientServer와 fanout descriptor가 서로 섞이지 않는다.
- `Draining` target이 새 selection에서 제외되고 accepted infrastructure work는 deadline까지 진행된다.
