# RouteMesh topology

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Channel 메시징](11-channel-messaging.ko.md)

이 문서는 ZLink Framework 11.0.0에서 물리 RouteMesh와 논리 ChannelName membership을 어떻게 구성하는지
정의한다. 대상 독자는 Framework topology와 startup validation을 구현하는 개발자다.

## 1. MeshName과 MeshNode

서로 통신할 node는 같은 `MeshName`의 RouteMesh에 참여한다. `MeshName`은 물리 연결망과 routing ID
namespace를 구분한다. 같은 process에는 같은 `MeshName`의 MeshNode를 하나만 등록할 수 있다. 서로 다른
이름의 MeshNode는 여러 개 등록할 수 있으며 mesh 사이의 자동 relay는 없다.

MeshNode 하나는 routing ID 하나와 peer가 연결할 ROUTER endpoint 하나를 가진다. endpoint를 bind하고
실제 주소를 확정한 뒤 descriptor를 게시한다. routing ID, MeshName과 endpoint identity는 MeshNode가
동작하는 동안 바꿀 수 없다.

## 2. ChannelName 역할과 membership

`ChannelName`은 application이 Channel send와 request의 대상을 지정하는 논리 주소다. RouteMesh builder는
각 ChannelName을 `Client` 또는 `Server` 역할로 등록한다. 두 역할 모두 별도 socket이나 endpoint를 만들지
않고 같은 MeshNode ROUTER 연결을 사용한다.

| 역할 | 등록 결과 |
|---|---|
| Client | 이 process의 ChannelName 송신 경로를 해당 MeshNode에 연결한다. Peer descriptor에는 target membership으로 게시하지 않는다 |
| Server | 송신 경로와 target membership을 등록하고 handler namespace와 weight를 제공한다 |

Server는 같은 ChannelName으로 outbound 호출도 시작할 수 있으므로 Client를 중복 등록하지 않는다.
`SetWeight(0)`은 Server를 새 선택에서 제외하는 값이며 Client 역할을 뜻하지 않는다.

MeshNode는 Server membership 없이도 시작할 수 있다. 이 구성은 Channel 호출 또는 Node direct 호출만
시작하는 process에 사용한다. Peer descriptor의 ChannelName set은 빈 값이며 가짜 ChannelName이나 weight
`0` membership을 요구하지 않는다.

Client와 Server role set은 startup 뒤 변경할 수 없다. 각 Server membership의 weight는 `0..100`이며 실행
중 변경할 수 있다. Weight 0은 새 ChannelName select-one과 Logical Multicast remote target에서 제외한다.
RID direct, 다른 membership과 이미 제출한 operation에는 영향을 주지 않는다.

Local Server weight를 실행 중 바꾸는 operation은 ChannelName으로 membership을 지정한다. 같은 process에서
ChannelName이 물리 topology 하나에만 속하므로 MeshName을 다시 요구하지 않는다. 선택된 physical MeshName,
RID와 descriptor revision은 runtime monitoring이 제공한다.

ChannelName은 하나의 process에서 물리 Channel topology 하나에만 대응해야 한다. 호출을 시작하는 역할에서는
그 topology의 송신 경로 하나를 가리킨다. 같은 이름을 서로 다른 RouteMesh, RouteMesh와 ClientServer 또는
서로 다른 ClientServer 등록에 사용하면 역할과 관계없이 startup이 실패한다. 같은 RouteMesh 또는 같은
ClientServer target 집합의 여러 process가 같은 Server ChannelName에 참여하는 것은 허용한다.
서로 연결되지 않은 process의 전체 등록을 검사하기 위해 location store나 전역 catalog를 요구하지 않는다.
Process 밖의 이름 일관성은 배포 구성이 검증하며 runtime의 중복 판정은 local 등록을 기준으로 한다.

ChannelName을 다른 topology로 옮길 때 한 host 안에 이전·새 등록을 동시에 두는 전환은 허용하지 않는다.
이전 host registration을 종료하고 새 topology로 구성한 host를 시작한다. Framework는 두 송신 경로 사이의
live migration, relay 또는 pending request 이전을 제공하지 않는다.

ChannelName handler는 `(ChannelName, message kind, packet identity)`로 구분한다. ChannelName이 process-local
송신 경로를 유일하게 결정하므로 application handler context에 MeshName을 제공하지 않는다. Node direct
handler는 MeshNode의 MeshName·RID route namespace에 등록하므로 별도 context와 key를 사용한다.

## 3. Full mesh와 peer admission

같은 MeshName의 ready MeshNode는 서로 직접 연결된 full mesh를 구성한다. Node 수가 `N`이면 각 MeshNode의
ROUTER가 관리하는 peer 연결은 최대 `N-1`개다. Framework는 별도 channel socket을 만들지 않으므로
ChannelName 수가 peer 연결 수를 늘리지 않는다.

양쪽에서 동시에 연결을 시도해도 admission은 RID와 lifecycle generation이 같은 peer 연결을 하나만
ready 상태로 유지한다. 중복 연결을 정리하는 내부 선택 규칙은 application이 관찰하는 메시징 의미를
바꾸지 않는다. Peer handshake는 다음 descriptor를
검증한다.

- MeshName, RID, lifecycle generation과 descriptor revision
- immutable Server ChannelName set과 channel별 weight. Set은 비어 있을 수 있다
- endpoint와 security identity
- protocol version과 필수 capability

MeshName 또는 trust profile이 다르거나 같은 lifecycle identity의 RID가 중복되면 admission하지 않는다.
Lifecycle generation은 non-zero opaque equality token이며 숫자 크기로 새 lifecycle을 판단하지 않는다. Automatic
MeshNode replacement는 새 RID와 새 token을 사용한다. Fixed RID manual topology의 재연결은 configured intent,
authenticated connection handover와 service liveness가 이전 pipe 종료를 확정한 뒤 다른 token을 ready로 만든다.
이전 token의 늦은 frame과 event는 current connection을 변경하지 못한다.

Descriptor revision은 같은 lifecycle 안의 mutable
weight snapshot을 구분하며 1 이상에서 단조 증가한다. weight를 바꾸면 owner는 revision을 증가시키고
Redis descriptor와 admitted peer control에 같은 snapshot을 게시한다. peer는 같은 lifecycle generation의
더 큰 revision만 적용하고 channel ready index를 원자적으로 교체한다. update가 유실되어도 다음 Redis
polling 또는 handshake가 최신 revision으로 수렴한다. weight 변경만으로 connection을 다시 만들지 않는다.

## 4. Peer를 찾는 방법

| 방식 | peer endpoint의 출처 | location store |
|---|---|---|
| Automatic | Redis location store의 MeshNode descriptor | 공식 Redis extension을 명시적으로 등록해야 한다 |
| Manual | application이 endpoint와 optional expected RID를 등록 | peer 연결만 사용하면 필요하지 않다 |

Manual mode도 같은 handshake와 admission을 사용한다. expected RID를 지정한 연결은 remote RID가 다르면
실패한다. expected RID를 생략하면 handshake가 identity를 확정한다. Manual peer 연결과 Spot·Actor location
조회는 서로 다른 기능이다. 분산 Spot·Actor 주소 또는 Actor transfer를 사용하면 peer 연결 방법과 관계없이
Redis location store를 등록해야 한다.

## 5. Readiness와 선택

MeshNode는 ROUTER bind, peer admission과 구성한 local handler·Spot·Actor registration이 준비된 뒤 ready가 된다.
ChannelName member는 MeshNode가 ready이고 해당 membership weight가 양수일 때 새 select-one 대상이 된다.
선택과 submit은 한 operation이므로 Framework가 선택한 RID를 application에 반환하지 않는다.

Client role은 local 송신 경로만 만들며 remote ready member 수에 포함되지 않는다. 같은 ChannelName의 Server
role이 없는 MeshNode도 remote Server membership을 선택해 send/request를 시작할 수 있다.

Drain을 시작한 MeshNode는 새 ChannelName 선택과 Logical Multicast target에서 제외된다. 이미 제출한
operation과 RID direct traffic의 종료 의미는 [Graceful drain](54-graceful-drain-handoff.ko.md)이 정의한다.

## 6. 서버 socket 설정

MeshNode의 transport 설정은 startup 전에 확정한다. 실행 중에는 ChannelName weight만 바꿀 수 있다.

`MaxMessageSize`는 byte 단위다. 양수는 수신하는 전체 transport message의 상한이며 `0`은 Framework가
별도 상한을 적용하지 않는다는 뜻이다. 음수는 설정 오류다. 각 언어 service runtime은 startup에서 이 값을
검증하고 transport별 설정값으로 변환한다.

상한을 넘긴 message는 일부 payload를 handler에 전달하지 않는다. Request header도 완성되지 않아 reply를
만들 수 없는 경우 호출자는 request timeout으로 완료된다. 이 실패는 같은 RouteMesh의 이후 정상 크기
message 처리를 막지 않는다. Application handler나 E2E가 decoding된 payload 길이를 검사하는 방식으로
transport 상한을 대신하지 않는다.

## 7. Classic fanout 경계

Classic fanout은 독립 PUB/SUB socket을 사용하는 기능이다. Fanout channel은 RouteMesh full mesh와
ChannelName membership에 참여하지 않으며 MeshNode가 필요하지 않다. 같은 host가 두 기능을 함께 사용할
수 있지만 endpoint, delivery policy와 monitoring은 각각의 계약을 따른다. Publisher endpoint는 fanout
전용 descriptor에 게시하고 automatic subscriber는 같은 fanout ChannelName의 publisher만 연결한다.
RouteMesh 또는 ClientServer descriptor를 fanout 연결 대상으로 해석하지 않는다.

Subscriber는 automatic descriptor의 publisher마다 전용 SUB socket을 하나 만들며, manual mode에서도
endpoint마다 전용 SUB socket을 사용한다. 여러 publisher endpoint를 한 SUB socket에 연결하지 않는다. PUB/SUB
message에는 source connection identity가 없으므로 socket을 공유하면 수신 activity와 timeout을 특정 publisher에
연결할 수 없기 때문이다. Fanout connection의 ready와 liveness는
[Transport liveness](55-transport-liveness.ko.md)의 단방향 beacon 계약을 따른다.

## 8. 검증 요구

- 같은 process의 중복 MeshName이 startup에서 실패한다.
- 같은 process에서 ChannelName이 서로 다른 물리 송신 경로에 중복 등록되면 startup에서 실패한다.
- 서로 다른 MeshName의 RID와 ChannelName member가 섞이지 않는다.
- 한 MeshNode의 복수 ChannelName이 같은 ROUTER peer 연결을 사용한다.
- Client role은 descriptor에 target membership으로 게시되지 않고 Server role만 weight와 handler를 가진다.
- Server membership이 0개인 MeshNode가 가짜 membership 없이 Node direct와 Channel outbound를 사용한다.
- Manual과 Automatic peer가 같은 handshake 및 duplicate-pipe 규칙을 따른다.
- Automatic fanout subscriber가 같은 ChannelName publisher만 연결하고 subscriber끼리 물리 연결을 만들지 않는다.
- Fanout subscriber가 publisher endpoint마다 전용 SUB socket을 사용하고 한 publisher의 timeout을 다른
  publisher에 적용하지 않는다.
- weight 0과 drain이 새 ChannelName 선택에만 적용된다.
- Startup에 고정한 `MaxMessageSize` 경계값과 초과 뒤 정상 request 처리를 검증한다.
