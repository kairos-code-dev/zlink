# RouteMesh topology

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Channel 메시징](11-channel-messaging.ko.md)

이 문서는 ZLink Framework 10.0.0에서 물리 RouteMesh와 논리 ChannelName membership을 어떻게 구성하는지
정의한다. 대상 독자는 Framework topology와 startup validation을 구현하는 개발자다.

## 1. MeshName과 MeshNode

서로 통신할 node는 같은 `MeshName`의 RouteMesh에 참여한다. `MeshName`은 물리 연결망과 routing ID
namespace를 구분한다. 같은 process에는 같은 `MeshName`의 MeshNode를 하나만 등록할 수 있다. 서로 다른
이름의 MeshNode는 여러 개 등록할 수 있으며 mesh 사이의 자동 relay는 없다.

MeshNode 하나는 routing ID 하나와 peer가 연결할 ROUTER endpoint 하나를 가진다. endpoint를 bind하고
실제 주소를 확정한 뒤 descriptor를 게시한다. routing ID, MeshName과 endpoint identity는 MeshNode가
동작하는 동안 바꿀 수 없다.

## 2. ChannelName membership

`ChannelName`은 같은 RouteMesh 안에서 service 역할을 구분하는 논리 이름이다. 별도 socket이나 endpoint를
만들지 않는다. MeshNode는 하나 이상의 ChannelName에 참여할 수 있으며 등록된 모든 membership이 같은
ROUTER 연결을 사용한다.

ChannelName set은 startup 뒤 변경할 수 없다. 각 membership의 weight는 `0..100`이며 실행 중 변경할 수
있다. weight 0은 새 ChannelName select-one과 Logical Multicast remote target에서 제외한다. RID direct,
다른 membership과 이미 제출한 operation에는 영향을 주지 않는다.

ChannelName handler는 `(MeshName, ChannelName, message kind, packet identity)`로 구분한다. Node direct
handler는 MeshNode의 RID route namespace에 등록하므로 ChannelName handler와 충돌하지 않는다.

## 3. Full mesh와 peer admission

같은 MeshName의 ready MeshNode는 서로 직접 연결된 full mesh를 구성한다. Node 수가 `N`이면 각 MeshNode의
ROUTER가 관리하는 peer 연결은 최대 `N-1`개다. Framework는 별도 channel socket을 만들지 않으므로
ChannelName 수가 peer 연결 수를 늘리지 않는다.

양쪽에서 동시에 연결을 시도해도 admission은 RID와 lifecycle generation이 같은 peer 연결을 하나만
ready 상태로 유지한다. 중복 연결을 정리하는 내부 선택 규칙은 application이 관찰하는 메시징 의미를
바꾸지 않는다. Peer handshake는 다음 descriptor를
검증한다.

- MeshName, RID, lifecycle generation과 descriptor revision
- immutable ChannelName set과 channel별 weight
- endpoint와 security identity
- protocol version과 필수 capability

MeshName 또는 trust profile이 다르거나 같은 generation의 RID가 중복되면 admission하지 않는다. 더 높은
generation은 해당 RID의 이전 pipe를 drain한 뒤 ready member가 된다.

lifecycle generation은 같은 RID의 재시작만 구분한다. descriptor revision은 같은 lifecycle 안의 mutable
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

MeshNode는 ROUTER bind, peer admission, local handler와 Spot·Actor registration이 준비된 뒤 ready가 된다.
ChannelName member는 MeshNode가 ready이고 해당 membership weight가 양수일 때 새 select-one 대상이 된다.
선택과 submit은 한 operation이므로 Framework가 선택한 RID를 application에 반환하지 않는다.

Drain을 시작한 MeshNode는 새 ChannelName 선택과 Logical Multicast target에서 제외된다. 이미 제출한
operation과 RID direct traffic의 종료 의미는 [Graceful drain](54-graceful-drain-handoff.ko.md)이 정의한다.

## 6. 서버 socket 설정

MeshNode의 transport 설정은 기본적으로 startup 전에 확정한다. 실행 중 변경할 수 있는 항목은 ChannelName
weight와 최대 수신 메시지 크기다.

`MaxMessageSize`는 byte 단위다. 양수는 수신하는 전체 transport message의 상한이며 `0`은 Framework가
별도 상한을 적용하지 않는다는 뜻이다. 음수는 설정 오류다. 실행 중 값을 바꾸면 MeshNode의 ROUTER에 즉시
적용한다. Core adapter는 Framework 값 `0`을 Core의 무제한 값 `-1`로 변환하고 양수는 그대로 전달한다.

상한을 넘긴 message는 일부 payload를 handler에 전달하지 않는다. Request header도 완성되지 않아 reply를
만들 수 없는 경우 호출자는 request timeout으로 완료된다. 이 실패는 같은 RouteMesh의 이후 정상 크기
message 처리를 막지 않는다. Application handler나 E2E가 decoding된 payload 길이를 검사하는 방식으로
transport 상한을 대신하지 않는다.

## 7. Classic fanout 경계

Classic fanout은 독립 PUB/SUB socket을 사용하는 기능이다. Fanout channel은 RouteMesh full mesh와
ChannelName membership에 참여하지 않으며 MeshNode가 필요하지 않다. 같은 host가 두 기능을 함께 사용할
수 있지만 endpoint, delivery policy와 monitoring은 각각의 계약을 따른다.

## 8. 검증 요구

- 같은 process의 중복 MeshName과 ChannelName 없는 MeshNode가 startup에서 실패한다.
- 서로 다른 MeshName의 RID와 ChannelName member가 섞이지 않는다.
- 한 MeshNode의 복수 ChannelName이 같은 ROUTER peer 연결을 사용한다.
- Manual과 Automatic peer가 같은 handshake 및 duplicate-pipe 규칙을 따른다.
- weight 0과 drain이 새 ChannelName 선택에만 적용된다.
- `MaxMessageSize` 변경 전후의 경계값과 이후 정상 request 처리를 검증한다.
