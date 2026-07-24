# .NET routing ID identity 공개 계약

[.NET exact interface 목차](README.ko.md) · [Topology configuration](03-configuration-topology.ko.md) ·
[Location record](08-location-maintenance.ko.md) ·
[Redis descriptor owner CAS](../../../41-location-store-redis.ko.md#7-routing-id-descriptor-owner-cas)

## 1. 범위

이 문서는 automatic discovery를 사용하는 MeshNode의 routing ID 생성과 descriptor owner claim 계약을 고정한다.
Public slot count·allocation group·slot store·allocation result와 readiness provider는 제공하지 않는다. Automatic
RID는 Framework가 lifecycle마다 만들고 `IZLinkMeshNodeLocationStore.UpdateMeshNodeAsync(...)`의 `NewClaim`으로
active owner 충돌을 확인한다.

## 2. Builder surface

`IZLinkMeshNodeBuilder.SetRoutingIdPrefix(string prefix)`와 manual topology용
`SetRoutingId(RoutingId routingId)`의 exact declaration은
[Topology configuration §2](03-configuration-topology.ko.md#2-등록-인터페이스)가 소유한다.

`SetRoutingIdPrefix(...)`의 prefix는 ASCII `[A-Za-z0-9._-]` 1..64자다. 생략하면 Framework가 listener
종류에 맞는 기본 prefix를 사용한다. Full RID는 `prefix-<uuid>`이고 UTF-8 encoded 크기는 255 bytes 이하다.
`uuid`는 Framework가 만든 RFC 4122 UUID v4의 lowercase canonical 36자 형식(`8-4-4-4-12`)이다. Prefix와
UUID를 placement, shard 또는 stable application identity로 해석하지 않는다.

같은 builder에서 routing mode를 두 번 설정하면 `ZLinkConfigurationException`이다. Fixed RID는 Location Store
descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만 허용한다. Object role이
`Client` 또는 `Server`이거나 automatic mode와 fixed RID를 함께 설정하면 startup이 실패한다.

## 3. Descriptor owner claim

Framework는 host owner lease를 claim한 뒤 candidate RID를 포함한 complete `ZLinkMeshNodeDescriptor`를 만들고
다음 기존 store operation을 호출한다.

`UpdateMeshNodeAsync(descriptor, ZLinkLocationWriteIntent.NewClaim, cancellationToken)`을 호출한다. Method의 exact
declaration은 [Location record §5](08-location-maintenance.ko.md#5-store-capability)가 소유한다.

Provider는 `(MeshName, Rid)`와 exact `ZLinkLocationOwnerToken`을 한 transaction에서 비교한다. Object Server의
`NewClaim`은 같은 transaction에서 `EntrySpotId`의 global Spot identity도 claim한다. 별도 Entry claim public
method는 제공하지 않는다. 두 identity 모두 충돌하지 않으면 descriptor와 Entry claim을 저장하고 `Stored`를
반환한다. 어느 하나라도 다른 active owner와 충돌하면 descriptor, Entry claim과 index를 바꾸지 않고
`RejectedConflict`를 반환한다. 같은 descriptor와 owner token의 재호출은 idempotent하다. Active conflict는
첫 claim에서 즉시 `RoutingIdConflict` configuration failure로 startup을 끝낸다. Framework는 두 번째 UUID나
claim을 만들지 않는다.

`EntrySpotId`는 descriptor immutable field와 digest에 포함되며 Renew나 mutable update로 바꿀 수 없다.
Release와 owner cleanup은 descriptor key, lifecycle generation과 같은 owner token을 exact 비교하고 일치할 때만
descriptor와 Entry claim을 함께 제거한다. Stale token은 `IgnoredStale`이고 successor descriptor나 Entry claim을
바꾸지 않는다. Replacement lifecycle은 이전 RID를 재사용하지 않고 새 RID와 lifecycle generation으로 새 claim을
수행한다. Redis provider는 RID format이나 prefix를 해석하지 않는다.

Claim attempt count는 1이다. Failure는 충돌한 UUID나 다른 owner token을 application에 노출하지
않는다. Diagnostic prefix와 최종 RID는 monitoring snapshot에서만 확인하며 metric label로 사용하지 않는다.

## 4. Startup 순서

1. Routing mode, prefix, object role과 Location Store 조합을 검증한다.
2. Object role 또는 automatic discovery가 Store를 요구하면 host owner lease를 한 번 claim한다.
3. Automatic mode는 candidate RID를 만들고 descriptor owner claim을 한 번 수행한다.
4. Claim한 RID로 socket을 bind하고 actual advertised endpoint를 확정한다.
5. 같은 owner token으로 complete descriptor를 갱신한 뒤 peer admission과 readiness를 연다.

Bind 또는 descriptor 게시가 실패하면 exact owner token으로 descriptor를 제거한 뒤 host owner lease를 마지막에
release한다. Runtime은 실행 중 충돌이나 연결 장애를 이유로 RID를 바꾸지 않는다. 새 RID는 새 lifecycle에서만
발급한다.

## 5. Entry Spot ID

Object Server lifecycle의 Entry Spot ID는 Framework가 발급한다. MeshNode와 같은 diagnostic prefix를
사용하고 MeshNode UUID와 독립적으로 생성한 RFC 4122 UUID v4의 lowercase canonical 36자
형식(`8-4-4-4-12`)으로 `<prefix>-entry-<entry-uuid>`를 만든다. Full RID는 UTF-8로 255 bytes 이하다.
Full MeshNode RID에 marker나 suffix를 이어 붙이는 방식은 금지한다. Prefix를 설정하지 않으면 같은
lifecycle의 automatic MeshNode RID에 사용한 Framework 기본 diagnostic prefix를 사용한다.

같은 lifecycle에서는 Entry Spot ID가 바뀌지 않는다. Endpoint가 같은 replacement lifecycle도 새 MeshNode
RID와 새 Entry Spot ID를 각각 발급한다. Global Spot namespace의 active conflict가 있으면 첫 claim에서
startup을 `RoutingIdConflict`로 끝내며 두 번째 Entry UUID나 claim을 만들지 않는다.

`ZLinkMeshNodeDescriptor.EntrySpotId`는 Object Server가 실제로 발급하고 `NewClaim`에서 함께 예약한 Entry
Spot ID를 그대로 게시한다. Actor placement·join과 relocation은 descriptor의 RID와 lifecycle generation을
exact mapping으로 사용하고 문자열을 parse하지 않는다. User Spot과 Instance Spot의 `Reserve`도 같은 global
identity claim과 충돌하는 RID를 거부하며 authority나 capacity를 일부 변경하지 않는다. Entry Spot ID를
application이 지정하거나 별도로 claim하는 public setter 또는 method는 제공하지 않는다.

## 6. 검증 요구

- Automatic MeshNode RID와 Entry Spot ID가 서로 독립적인 lowercase canonical UUID v4를 사용하며 충돌 시
  첫 claim에서 즉시 실패하고 두 번째 UUID나 claim을 만들지 않는지 검증한다.
- Object Server `NewClaim`이 MeshNode descriptor와 Entry Spot global identity를 한 transaction에서 만들고,
  어느 한 identity의 충돌에도 부분 상태를 남기지 않는지 검증한다.
- User Spot과 Instance Spot `Reserve`가 active Entry claim과 충돌하는 RID를 거부하는지 검증한다.
- Exact owner token과 lifecycle cleanup만 Entry claim을 해제하며 stale cleanup이 successor claim을 제거하지
  못하는지 검증한다.
- Renew와 mutable update가 immutable `EntrySpotId` 또는 descriptor digest를 바꾸지 못하는지 검증한다.
