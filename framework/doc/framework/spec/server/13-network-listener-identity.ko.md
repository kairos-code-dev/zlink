# Network listener identity — 공통 스펙

[스펙 목차](../README.ko.md) · [Channel topology](10-channel-topology.ko.md) ·
[ClientServer Channel](12-client-server-channel.ko.md) · [Location runtime](40-location-runtime.ko.md)

## 1. 범위

이 문서는 Framework가 만드는 network listener의 local bind 주소와 remote process에 제공할 접속 주소를
구분한다. RouteMesh, ClientServer Channel, classic fanout publisher와 STREAM server는 같은 process 기본
network identity를 사용하며, 필요한 listener만 별도 값을 지정한다.

HTTP listener는 server hosting package의 URL 계약을 따르며 이 문서의 자동 location record 대상이 아니다.

## 2. Process 기본값과 listener별 값

Framework root는 process 기본 `BindHost`와 `AdvertiseHost`를 가진다.

| 값 | 의미 |
|---|---|
| BindHost | local listener가 bind할 network interface |
| AdvertiseHost | remote process가 실제로 연결할 host 또는 주소 |

Listener별 override가 있으면 그 listener에만 적용하고, 없으면 process 기본값을 사용한다. 한 listener의
override가 다른 RouteMesh, ClientServer, fanout 또는 STREAM listener의 endpoint를 바꾸지 않는다.

`0.0.0.0`과 `::`는 BindHost로 사용할 수 있지만 AdvertiseHost로 사용할 수 없다. Remote에서 접속할 수
있는 AdvertiseHost를 결정할 수 없으면 endpoint 또는 discovery record를 게시하기 전에 startup이 실패한다.

Process 기본 BindHost는 `127.0.0.1`이다. AdvertiseHost를 생략하고 BindHost가 wildcard가 아니면 같은 host를
advertised host로 사용한다. BindHost가 wildcard이면 AdvertiseHost를 명시해야 한다. 이 기본값은 local
실행을 위한 것이며 container나 여러 host 배포에서는 remote process가 연결할 수 있는 주소를 명시한다.

## 3. Port 확정

Listener는 고정 port를 사용하거나 Framework에 빈 port 할당을 맡길 수 있다. 자동 할당은 port `0`으로
bind한 뒤 실제 bound port를 읽는 방식이다.

Automatic discovery listener는 port를 생략하면 `0`을 사용한다. Manual mode에서 remote process가 listener
endpoint를 얻을 별도 discovery source가 없으면 server listen endpoint와 client의 remote endpoint를
명시해야 한다.

```text
Bind endpoint       = BindHost + configured or allocated port
Advertised endpoint = AdvertiseHost + actual bound port
```

Wildcard host와 port `0`은 local bind 입력에만 사용할 수 있다. Advertised endpoint, location record와
manual peer 설정에 남아 있으면 startup 설정 오류다.

## 4. Topology별 record 경계

확정한 advertised endpoint는 listener 종류에 맞는 record 또는 설정에만 기록한다.

| Listener | Remote에 제공하는 위치 | 금지되는 기록 |
|---|---|---|
| RouteMesh MeshNode | MeshName과 RID로 식별하는 MeshNode descriptor의 Endpoint | ClientServer server descriptor |
| ClientServer server | ChannelName과 server identity로 식별하는 ClientServer server descriptor의 Endpoint | MeshNode descriptor와 Spot·Actor location row |
| Classic fanout publisher | ChannelName과 Publisher RID로 식별하는 fanout publisher descriptor의 Endpoint | MeshNode와 ClientServer descriptor |
| STREAM server | 기존 STREAM endpoint 설정 또는 해당 기능의 별도 discovery 계약 | MeshNode와 ClientServer descriptor |

Automatic discovery에 참여하는 classic fanout publisher는 fanout 전용 descriptor를 location store에
게시한다. Subscriber는 같은 ChannelName의 publisher descriptor만 조회한다. Store가 없는 publisher는
descriptor를 게시하지 않고 고정 endpoint를 manual subscriber에 제공한다. STREAM endpoint는 location
store에 자동 게시하지 않는다.

## 5. Lifecycle과 identity

AdvertiseHost 또는 실제 bound port가 바뀐 listener 재시작은 해당 topology의 새 lifecycle generation과
endpoint를 같은 descriptor revision에 기록한다. Endpoint만 바꾸고 이전 lifecycle generation을 유지하지
않는다. Remote runtime은 record의 identity와 generation을 transport 연결에서 다시 확인한 뒤 ready로
판정한다.

RouteMesh, ClientServer와 classic fanout이 같은 process에 있어도 각 listener는 자기 descriptor와 lifecycle generation을
소유한다. 한쪽 endpoint 변경을 다른 topology의 generation 또는 descriptor 변경으로 해석하지 않는다.

Automatic discovery에 참여하는 RouteMesh MeshNode의 RID는 MeshNode lifecycle마다 Framework가 새로 만든 opaque
transport identity다. Caller는 ASCII `[A-Za-z0-9._-]` 1..64자의 진단 prefix만 지정할 수 있고, Framework는
128-bit CSPRNG 값을 32자리 lowercase hex로 encode해 `prefix-<suffix>`를 만든다. Full RID는 255 bytes 이하다.
Prefix와 RID를 application identity, placement, shard나 stable host 이름으로 해석하지 않는다.

MeshNode descriptor owner CAS는 `(MeshName, RID)`의 active conflict를 확인한다. Framework는 충돌할 때 새 RID를
최대 8회 만들고 계속 충돌하면 `RoutingIdConflict`로 startup을 실패한다. Replacement MeshNode lifecycle은
endpoint가 같아도 새 RID를 사용한다. Fixed MeshNode RID는 descriptor와 automatic discovery를 사용하지 않는
explicit manual RouteMesh topology에서만 허용한다. ClientServer와 classic fanout identity는 각 topology의 별도
계약을 따른다.

Kubernetes에서는 Pod IP 또는 Pod별 DNS 이름을 AdvertiseHost로 사용할 수 있다. 개별 RID와 server identity,
weight, admission과 drain을 관찰해야 하는 listener는 여러 Pod를 하나의 일반 Service 가상 주소로 대신하지
않고 개별 Pod endpoint를 발견할 수 있어야 한다.

## 6. 검증 요구

- RouteMesh, ClientServer, classic fanout과 STREAM listener가 process 기본값과 listener override 우선순위를
  동일하게 적용한다.
- 자동 port 할당 뒤 advertised endpoint의 port가 실제 bound port와 일치한다.
- Wildcard host와 port `0`이 remote endpoint나 location record에 남지 않는다.
- RouteMesh, ClientServer와 fanout endpoint가 서로 다른 descriptor 종류에 기록되고 Spot·Actor row에 복제되지 않는다.
- Advertised endpoint가 바뀐 재시작에서 새 generation만 ready가 된다.
- Automatic MeshNode가 prefix와 random suffix 형식의 RID를 사용하고 active conflict를 최대 8회 재시도한다.
- Replacement MeshNode lifecycle이 새 RID를 사용하며 fixed RID와 automatic discovery를 함께 설정할 수 없다.
- 같은 container port를 사용하는 여러 Pod가 서로 다른 AdvertiseHost로 직접 연결된다.
