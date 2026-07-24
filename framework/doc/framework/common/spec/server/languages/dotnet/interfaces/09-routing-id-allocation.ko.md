# .NET routing ID identity 공개 계약

[.NET exact interface 목차](README.ko.md) · [Topology configuration](03-configuration-topology.ko.md) ·
[Location record](08-location-maintenance.ko.md) ·
[Redis descriptor owner CAS](../../../../41-location-store-redis.ko.md#7-routing-id-descriptor-owner-cas)

## 1. 범위

이 문서는 automatic discovery를 사용하는 MeshNode의 routing ID 생성과 descriptor owner claim 계약을 고정한다.
[Descriptor](../../../../01-glossary.ko.md#descriptor) [owner](../../../../01-glossary.ko.md#owner) claim은 같은 MeshName과 RID를 다른 lifecycle이 이미 사용 중인지 Store에서 확인하고, 비어
있을 때 현재 host의 owner token으로 [MeshNode](../../../../01-glossary.ko.md#meshnode) descriptor를 저장하는 절차다.
Public slot count·allocation group·slot store·allocation result와 readiness provider는 제공하지 않는다. Automatic
RID는 Framework가 lifecycle마다 만들고 `IZLinkMeshNodeLocationStore.UpdateMeshNodeAsync(...)`의 `NewClaim`으로
active owner 충돌을 확인한다.

## 2. Builder surface

`IZLinkMeshNodeBuilder.SetRoutingIdPrefix(string prefix)`와 manual topology용
`SetRoutingId(RoutingId routingId)`의 exact declaration은
[Topology configuration §2](03-configuration-topology.ko.md#2-등록-인터페이스)가 소유한다.

`SetRoutingIdPrefix(...)`의 prefix는 ASCII `[A-Za-z0-9._-]` 1..64자다. 생략하면 Framework가 listener
종류에 맞는 기본 prefix를 사용한다. Full RID는 `prefix-<lowercase-canonical-uuid-v4>`이고 UTF-8 encoded
크기는 255 bytes 이하다. UUID v4는 `8-4-4-4-12` 자리의 lowercase canonical 문자열로 표현한다. Prefix와
UUID를 placement, shard 또는 stable application identity로 해석하지 않는다.

Object Server의 Entry Spot ID는 같은 prefix를 사용한
`<prefix>-entry-<lowercase-canonical-uuid-v4>` 형식이다. MeshNode와 Entry Spot에는 각각 별도로 생성한
UUID v4를 사용한다. `ZLinkMeshNodeDescriptor.EntrySpotId`가 같은 lifecycle의 exact mapping을 제공한다.
Global Spot ID가 active owner와 충돌하면 새 UUID로 다시 시도하지 않고 즉시 startup을
`SpotIdConflict`로 끝낸다. Caller가 지정한 User·Instance Spot ID가 이 예약 형식과 일치하면 Store operation 전에
`InvalidConfiguration`으로 거부한다.

같은 builder에서 routing mode를 두 번 설정하면 `ZLinkConfigurationException`이다. Fixed RID는 Location Store
descriptor와 [automatic discovery](../../../../01-glossary.ko.md#automatic-discovery)를 사용하지 않는 explicit manual topology에서만 허용한다. Object role이
`Client` 또는 `Server`이거나 automatic mode와 fixed RID를 함께 설정하면 startup이 실패한다.

## 3. Descriptor owner claim

Framework는 host owner lease를 claim한 뒤 candidate RID를 포함한 complete `ZLinkMeshNodeDescriptor`를 만들고
다음 기존 store operation을 호출한다.

`UpdateMeshNodeAsync(descriptor, ZLinkLocationWriteIntent.NewClaim, cancellationToken)`을 호출한다. Method의 exact
declaration은 [Location record §5](08-location-maintenance.ko.md#5-store-capability)가 소유한다.

Provider는 `(MeshName, Rid)`와 exact `ZLinkLocationOwnerToken`을 한 transaction에서 비교한다. Active owner가
없으면 descriptor를 저장하고 `Stored`를 반환한다. 같은 descriptor와 owner token의 재호출은 idempotent하다.
다른 active owner가 있으면 기존 descriptor를 바꾸지 않고 `RejectedConflict`를 반환한다. Framework는 새
UUID를 만들거나 claim을 다시 시도하지 않고 startup을 `RoutingIdConflict` configuration failure로 끝낸다.

Renew·mutable update·release는 descriptor key, lifecycle generation과 같은 owner token을 exact 비교한다. Stale
token은 `IgnoredStale`이고 current descriptor를 바꾸지 않는다. Replacement lifecycle은 이전 RID를 재사용하지
않고 새 RID와 [lifecycle generation](../../../../01-glossary.ko.md#lifecycle-generation)으로 새 claim을 수행한다. Redis provider는 RID format이나 prefix를 해석하지
않는다.

Failure는 충돌한 RID나 다른 owner token을 application에 노출하지 않는다. 재시도 횟수를 조정하는 public
option도 제공하지 않는다. Diagnostic prefix와 최종 RID는 monitoring snapshot에서만 확인하며 metric label로
사용하지 않는다.

## 4. Startup 순서

1. Routing mode, prefix, object role과 [Location Store](../../../../01-glossary.ko.md#location-store) 조합을 검증한다.
2. Object role 또는 automatic discovery가 Store를 요구하면 host [owner lease](../../../../01-glossary.ko.md#owner-lease)를 한 번 claim한다.
3. Automatic mode는 candidate RID를 한 번 만들고
   [descriptor owner claim](../../../../01-glossary.ko.md#meshnode-descriptor)을 한 번 수행한다.
4. Claim한 RID로 socket을 bind하고 actual advertised endpoint를 확정한다.
5. 같은 owner token으로 complete descriptor를 갱신한 뒤 peer admission과 readiness를 연다.

Bind 또는 descriptor 게시가 실패하면 exact owner token으로 descriptor를 제거한 뒤 host owner lease를 마지막에
release한다. Runtime은 실행 중 충돌이나 연결 장애를 이유로 RID를 바꾸지 않는다. 새 RID는 새 lifecycle에서만
발급한다.
