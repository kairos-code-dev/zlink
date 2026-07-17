# Location Runtime — 공통 스펙

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Redis location store](41-location-store-redis.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 MeshNode discovery와 Spot·Actor 위치 확인에 사용하는 공통 공개
계약을 정의한다. 이 문서는 “물리 MeshNode의 접속 정보와 논리 Spot·Actor의 현재 owner를 어떤 record로
구분하고, 어떤 구성에서 Redis가 필요한가?”라는 질문에 답한다.

MeshNode identity와 peer admission은 [21 MeshNode](21-mesh-node.ko.md), SpotHandle의 위치 투명성은
[24 Spot 주소 메시징](24-spot-address-messaging.ko.md), Redis key·원자성·연결 수명은
[41 Redis location store](41-location-store-redis.ko.md)가 소유한다. 이 문서는 Redis 내부 자료 구조를
반복해서 정의하지 않는다.

## 2. Record 분리

Location runtime은 물리 node discovery와 논리 object ownership을 서로 다른 record로 관리한다.

### 2.1 MeshNode descriptor

MeshNode descriptor는 RouteMesh peer admission과 연결 계획에 필요한 물리 identity를 나타낸다.

| 필드 | 계약 |
|---|---|
| MeshName | 물리 RouteMesh와 RID namespace를 구분하는 immutable 이름 |
| RID | 같은 MeshName에서 MeshNode를 식별하는 identity |
| Lifecycle generation | 같은 RID로 시작한 lifecycle을 구분하는 단조 증가 값 |
| Descriptor revision | 같은 lifecycle 안의 mutable descriptor snapshot을 구분하는 단조 증가 값 |
| Endpoint | peer가 연결할 ROUTER endpoint |
| ChannelName set | descriptor 게시 전에 고정하는 하나 이상의 immutable membership |
| Channel weight set | ChannelName별 현재 select-one weight |
| Drain state | 신규 channel·multicast 선택 가능 여부 |
| Security identity | peer admission에서 검증할 trust identity |
| Owner lease | descriptor를 게시한 runtime의 유효성 |

descriptor 하나가 MeshNode의 endpoint와 전체 ChannelName membership을 함께 나타낸다. ChannelName마다
별도 endpoint, descriptor 또는 membership record를 만들지 않는다.

ChannelName set은 lifecycle 동안 immutable이지만 weight와 drain state는 바뀔 수 있다. 이 두 필드가
바뀌면 descriptor revision을 증가시키고 row와 change stamp를 같은 store operation으로 갱신한다. reader는
RID와 lifecycle generation이 같은 descriptor 가운데 가장 큰 revision만 적용한다. 더 낮은 revision은
현재 ready index를 되돌리지 않는다.

### 2.2 Spot location row

Spot location row는 논리 Spot의 현재 owner를 나타낸다.

| 필드 | 계약 |
|---|---|
| MeshName, Spot RID | 논리 Spot identity |
| Owner RID, Owner generation | 현재 owner MeshNode identity |
| Spot generation | 같은 Spot RID의 activation을 구분하는 fencing 값 |
| Spot kind와 type | Entry 또는 user Spot과 등록된 Spot type |
| Owner lease | owner runtime의 유효성 |

Spot location row는 MeshNode ChannelName membership을 복제하지 않는다. resolver는 row의 MeshName과 owner
identity를 현재 유효한 MeshNode descriptor 및 owner lease와 함께 검증한다.

### 2.3 Actor location row

Actor location row는 논리 Actor의 현재 owner와 Spot membership을 나타낸다.

| 필드 | 계약 |
|---|---|
| MeshName, Actor ID, Actor type | 논리 Actor identity |
| Owner RID, Owner generation | Actor queue를 소유한 MeshNode identity |
| Actor generation | Actor lifecycle과 transfer fencing 값 |
| Membership epoch | 같은 Actor generation 안에서 Spot membership 변경을 구분하는 CAS 값 |
| Spot RID, Spot generation과 Spot kind | 현재 Spot membership과 같은 RID로 다시 만들어진 Spot을 구분하는 lifecycle fence |
| Owner lease | owner runtime의 유효성 |

Actor location row는 STREAM session route를 public location record로 제공하지 않는다. session binding은
[31 Session Actor Dispatch](31-session-actor-dispatch.ko.md)의 runtime state가 소유한다.

### 2.4 Owner lease

descriptor와 location row는 owner lease가 유효할 때만 성공 결과로 사용할 수 있다. row timestamp만으로
owner 생존을 판정하지 않는다. lease expiry는 store 기준 시각으로 판정하며 application node의 wall
clock을 authority로 사용하지 않는다.

같은 logical key의 새 generation은 store가 원자적으로 발급한다. 오래된 owner token의 renew·release는
현재 record를 바꾸지 않는다.

Location runtime은 heartbeat interval, owner lease TTL, store polling interval, store failure grace,
routing ID fencing margin과 owner lease renew timeout을 양수로 설정한다. Routing ID 자동 할당을 사용하면
다음 관계를 만족해야 한다. 이 관계를 만족하지 않으면 host는 socket bind 전에 설정 오류로 종료한다.

```text
heartbeat interval + owner lease renew timeout
    < owner lease TTL - routing ID fencing margin
```

언어별 exact interface는 같은 여섯 값을 해당 언어의 duration 타입과 이름으로 투영한다. 기본값과 공개
signature는 언어별 문서가 소유하지만 위 유효성 관계를 바꾸지 않는다.

## 3. Redis 등록 조건

다음 기능을 사용하는 host는 공식 Redis location store를 명시적으로 등록해야 한다.

| 기능 | Redis 등록 |
|---|---|
| MeshNode automatic discovery | 필수 |
| remote Spot resolve와 owner 갱신 | 필수 |
| remote Actor resolve와 owner 갱신 | 필수 |
| Actor transfer와 분산 fencing | 필수 |
| manual peer만 사용하는 MeshNode | 불필요 |
| process-local Spot·Actor만 사용하는 host | 불필요 |
| classic fanout만 사용하는 host | 불필요 |

필수 기능을 등록하면서 Redis location store를 제공하지 않으면 host startup이 실패한다. 공식 Redis
extension의 등록, key prefix, lease와 원자성 계약은
[41 Redis location store](41-location-store-redis.ko.md)가 정한다. process-local in-memory store는
contract test에서만 사용할 수 있다.

한 MeshNode에 manual peer intent와 Redis discovery result가 함께 들어올 수 있다. 같은 RID와 generation을
가리키는 중복 source는 peer intent 하나로 합치며, source에 따라 admission이나 messaging 의미를 나누지
않는다. automatic discovery를 활성화한 MeshNode는 manual intent도 있더라도 Redis 등록이 필요하다.

## 4. Manual peer mode

Manual peer mode에서는 application이 endpoint 또는 expected RID와 endpoint를 등록한다. remote
MeshNode의 MeshName, RID, generation과 ChannelName set은 admission handshake에서 받는다.

- expected RID가 있으면 handshake RID와 정확히 일치해야 한다.
- MeshName 또는 security identity가 다르면 admission하지 않는다.
- 같은 RID의 낮은 generation이나 충돌하는 descriptor는 ready member가 되지 않는다.
- Framework는 설정에 없는 peer를 찾지 않으므로 배포 설정이 필요한 peer 관계를 모두 제공해야 한다.
- admission 이후 Node·Channel·Spot·Actor 메시징 의미는 automatic discovery와 같다.

manual peer만 사용하는 경우에는 descriptor를 Redis에 게시하거나 조회하지 않는다. Redis discovery와
함께 사용하면 manual endpoint와 발견한 endpoint를 RID·generation 기준으로 합치고 같은 admission을
수행한다. 분산 Spot·Actor location을 함께 사용하면 object location을 위해서도 Redis 등록이 필요하다.

## 5. Automatic discovery

Automatic discovery를 사용하는 MeshNode는 자신의 descriptor와 owner lease를 Redis에 게시하고 같은
MeshName의 유효한 descriptor만 읽는다. Framework는 descriptor snapshot에서 peer connection intent를
계산하고 admission handshake로 실제 identity를 다시 검증한다.

- 다른 MeshName의 descriptor는 connection이나 ChannelName ready index에 포함하지 않는다.
- descriptor가 보인다는 사실만으로 peer를 ready로 간주하지 않는다.
- ready는 transport 연결, handshake, generation과 ChannelName 검증이 모두 성공한 뒤에 성립한다.
- descriptor의 drain state는 새 ChannelName과 Logical Multicast 선택에서 해당 node를 제외하지만 기존
  연결을 즉시 끊지 않는다.
- Redis 장애 중에는 마지막으로 성공한 peer intent를 유지하고 신규 connect·disconnect 계산을 멈춘다.

polling은 상태 수렴을 보장하는 기본 경로다. change notification이나 stamp를 사용할 수 있지만 event
유실 뒤에도 다음 polling snapshot으로 같은 결과에 도달해야 한다.

## 6. Publish와 resolve

MeshNode startup은 ROUTER endpoint가 확정되고 local registration 검증이 끝난 뒤 descriptor를 게시한다.
descriptor 게시 전에는 automatic peer의 ready 선택 대상이 될 수 없다.

Spot·Actor 생성은 owner claim이 성공한 뒤 object를 활성화하고 location row를 게시한다. transfer는
generation과 owner token으로 source와 target의 쓰기를 fence한다. transaction 순서는
[23 Spot Actor](23-spot-actor.ko.md)가 소유한다.

resolver는 다음 결과만 제공한다.

| 조회 | 성공 결과 |
|---|---|
| MeshName peers | 유효한 owner lease를 가진 MeshNode descriptor snapshot |
| Spot | 유효한 owner와 Spot generation을 가진 SpotHandle |
| Actor | 유효한 ActorRef 또는 현재 Spot을 가리키는 handle |

owner lease가 만료되었거나 owner descriptor의 lifecycle generation과 맞지 않는 location row는 성공 결과에서
제외한다. 물리 record cleanup 시점은 resolve 의미를 바꾸지 않는다.

## 7. Failure와 recovery

Redis read·write 실패는 infrastructure failure로 관측한다. automatic peer 계산과 신규 distributed
owner claim은 성공한 store operation 없이 진행하지 않는다. 이미 admitted된 peer 연결과 이미 local
queue에 수락한 메시지는 Redis 장애만으로 중단하지 않는다.

lease renew가 deadline 안에 회복되지 않으면 해당 runtime은 새 owner operation을 받지 않고 drain 또는
fenced 상태로 들어간다. store가 회복되면 descriptor와 location state를 현재 owner token으로 다시
검증한 뒤 automatic discovery를 재개한다. stale token이 거부되면 현재 record를 덮어쓰지 않는다.

## 8. 관측과 검증

Location 상태는 [50 Runtime monitoring](50-runtime-monitoring.ko.md)의 MeshNode snapshot과 location
event에서 관찰한다. 별도의 peer 조회 service를 추가하지 않는다.

다음 조건을 검증한다.

- MeshNode descriptor와 Spot·Actor location row가 서로 다른 key와 수명을 가진다.
- descriptor가 MeshName, RID, lifecycle generation, descriptor revision과 immutable ChannelName set을 함께 제공한다.
- automatic discovery와 distributed Spot·Actor 기능은 Redis 미등록 시 startup에서 실패한다.
- manual peer만 사용하는 MeshNode는 Redis 없이 handshake descriptor로 admission한다.
- manual peer와 Redis discovery가 같은 RID를 발견하면 peer intent 하나로 합쳐 같은 admission을 사용한다.
- 다른 MeshName, 낮은 generation과 owner lease가 만료된 record가 성공 결과에 포함되지 않는다.
- Redis 장애가 이미 admitted된 peer를 즉시 disconnect하지 않는다.
- recovery 뒤 current generation만 publish·resolve 결과로 사용한다.
