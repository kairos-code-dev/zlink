# Routing identity runtime

[Framework 공통 내부 구조](README.ko.md) ·
[Topology configuration](../spec/server/languages/dotnet/interfaces/03-configuration-topology.ko.md) ·
[Location Store](../spec/21-location-runtime.ko.md)

이 문서는 automatic discovery를 사용하는 MeshNode의 RID 생성과 descriptor owner claim을 구현하는 내부
순서를 설명한다. Application이 호출하거나 외부 provider가 추가로 구현하는 interface는 정의하지 않는다.
RID 형식과 builder validation처럼 caller가 알아야 하는 계약은 언어별 topology configuration 문서가 소유하고,
Store provider가 구현하는 operation은 언어별 Location Store SPI 문서가 소유한다.

## 1. Descriptor owner claim

Runtime은 host owner lease를 확보한 뒤 candidate RID를 포함한 complete MeshNode descriptor를 만든다. 이어서
Location Store의 descriptor write operation을 `NewClaim` intent로 호출한다. Provider는 `(MeshName, RID)`와
exact owner token을 한 transaction에서 비교한다.

- active owner가 없으면 descriptor를 저장한다.
- 같은 descriptor와 owner token의 반복 호출은 같은 결과를 반환한다.
- 다른 active owner가 있으면 기존 descriptor를 바꾸지 않는다.
- renew, mutable update와 release는 descriptor key, lifecycle generation과 같은 owner token을 exact 비교한다.
- stale token은 current descriptor를 바꾸지 않는다.

Framework는 active owner 충돌을 받으면 candidate UUID를 다시 만들지 않고 startup을 끝낸다. Replacement
lifecycle도 이전 RID를 재사용하지 않고 새 RID와 lifecycle generation으로 claim한다. Provider는 RID 형식이나
prefix를 해석하지 않는다.

## 2. Startup과 정리 순서

1. Runtime은 routing mode, prefix, object role과 Location Store 조합을 검증한다.
2. Object role 또는 automatic discovery가 Store를 요구하면 host owner lease를 한 번 claim한다.
3. Automatic mode에서는 candidate RID를 한 번 만들고 descriptor owner claim을 한 번 수행한다.
4. Claim한 RID로 socket을 bind하고 실제 advertised endpoint를 확정한다.
5. 같은 owner token으로 complete descriptor를 갱신한 뒤 peer admission과 readiness를 연다.

Bind 또는 descriptor 게시가 실패하면 exact owner token으로 descriptor를 제거하고 host owner lease를 마지막에
release한다. 실행 중 충돌이나 연결 장애는 RID를 바꾸는 조건이 아니다. 새 RID는 새 lifecycle에서만 만든다.

이 순서는 descriptor row와 socket identity가 서로 다른 lifecycle을 가리키는 구간을 만들지 않으며, 실패한
startup의 owner lease가 뒤에 남아 후속 lifecycle을 차단하지 않도록 한다.
