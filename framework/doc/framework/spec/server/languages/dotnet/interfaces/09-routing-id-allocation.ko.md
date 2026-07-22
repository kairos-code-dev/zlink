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
종류에 맞는 기본 prefix를 사용한다. Full RID는 `prefix-<32 lowercase hex>`이고 UTF-8 encoded 크기는 255
bytes 이하다. Suffix는 Framework가 만든 128-bit CSPRNG 값이다. Prefix와 suffix를 placement, shard 또는
stable application identity로 해석하지 않는다.

같은 builder에서 routing mode를 두 번 설정하면 `ZLinkConfigurationException`이다. Fixed RID는 Location Store
descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만 허용한다. Object role이
`Client` 또는 `Server`이거나 automatic mode와 fixed RID를 함께 설정하면 startup이 실패한다.

## 3. Descriptor owner claim

Framework는 host owner lease를 claim한 뒤 candidate RID를 포함한 complete `ZLinkMeshNodeDescriptor`를 만들고
다음 기존 store operation을 호출한다.

`UpdateMeshNodeAsync(descriptor, ZLinkLocationWriteIntent.NewClaim, cancellationToken)`을 호출한다. Method의 exact
declaration은 [Location record §5](08-location-maintenance.ko.md#5-store-capability)가 소유한다.

Provider는 `(MeshName, Rid)`와 exact `ZLinkLocationOwnerToken`을 한 transaction에서 비교한다. Active owner가
없으면 descriptor를 저장하고 `Stored`를 반환한다. 같은 descriptor와 owner token의 재호출은 idempotent하다.
다른 active owner가 있으면 기존 descriptor를 바꾸지 않고 `RejectedConflict`를 반환한다. Framework는 새 random
suffix를 만들어 최대 8회 claim하며 모두 충돌하면 startup을 `RoutingIdConflict` configuration failure로 끝낸다.

Renew·mutable update·release는 descriptor key, lifecycle generation과 같은 owner token을 exact 비교한다. Stale
token은 `IgnoredStale`이고 current descriptor를 바꾸지 않는다. Replacement lifecycle은 이전 RID를 재사용하지
않고 새 RID와 lifecycle generation으로 새 claim을 수행한다. Redis provider는 RID format이나 prefix를 해석하지
않는다.

Attempt count는 8이다. Failure는 충돌한 random suffix나 다른 owner token을 application에 노출하지
않는다. Diagnostic prefix와 최종 RID는 monitoring snapshot에서만 확인하며 metric label로 사용하지 않는다.

## 4. Startup 순서

1. Routing mode, prefix, object role과 Location Store 조합을 검증한다.
2. Object role 또는 automatic discovery가 Store를 요구하면 host owner lease를 한 번 claim한다.
3. Automatic mode는 candidate RID를 만들고 descriptor owner claim을 최대 8회 수행한다.
4. Claim한 RID로 socket을 bind하고 actual advertised endpoint를 확정한다.
5. 같은 owner token으로 complete descriptor를 갱신한 뒤 peer admission과 readiness를 연다.

Bind 또는 descriptor 게시가 실패하면 exact owner token으로 descriptor를 제거한 뒤 host owner lease를 마지막에
release한다. Runtime은 실행 중 충돌이나 연결 장애를 이유로 RID를 바꾸지 않는다. 새 RID는 새 lifecycle에서만
발급한다.
