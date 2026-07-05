# [DRAFT] auto-connect 단방향 연결 규칙 통일 (route mesh · spot mesh)

- 상태: 설계 확정(사용자 결정 2026-07-05) — 구현 진행
- 대상 버전: core 8.6.0 (동작 규칙 변경 — minor)
- 결정자: 사용자 (배경 논의: route mesh 양방향 connect의 대규모(1000+ node) 연결 오버헤드)

## 1. 배경과 현행 규칙

현행 auto-connect 대상 판정은 `core/src/runtime/services/discovery/discovery_protocol.hpp`
`socket_auto_connect_target_matches`에 있다:

| 유형 | 현행 | 방향성 |
|---|---|---|
| ROUTE_MESH (router↔router) | `compare_connect_keys(...) != 0` | **양방향**(자기 자신만 제외, 서로 dial) |
| SPOT_MESH (spot↔spot) | role 매칭만(호출부에서 같은 endpoint·같은 rid 제외) | **양방향** |
| DEALER_MESH | `compare_connect_keys(...) < 0` | 단방향(키 비교, 작은 쪽이 dial) |
| CLIENT_SERVER | dealer→router | 역할 기반 단방향 |
| FANOUT | sub→pub | 역할 기반 단방향 |

양방향이었던 이유: rid-addressed 송신(`request_to_node(rid)`, spot data plane,
route bridge)이 **자기가 dial한 링크에 결부**되는 전제(connect 시 rid 지정 —
dotnet `SetConnectRoutingId`, cpp `connect_routing_id`)라, 각 노드가 상대에게
보내려면 각자 dial이 필요했다.

## 2. 결정: 단방향 통일

ROUTE_MESH·SPOT_MESH를 DEALER_MESH와 동일한 **키 비교 단방향**으로 통일한다.

- 판정: `compare_connect_keys(local, remote) < 0` 인 쪽만 dial (같은 endpoint·같은 rid 제외 규칙은 유지)
- n(n-1) → n(n-1)/2 로 물리 연결 수 절반. 1000+ node mesh에서 실질 이득.

### 성립 전제 — 인바운드 rid 학습(probe)

non-initiator도 rid-addressed 송신이 가능해야 하므로:

1. **probe 기반 즉시 학습**: initiator가 connect하는 소켓에 probe(기존
   `ZLINK_INTERNAL_OPT_PROBE_ROUTER` 계열)를 활성화 — 연결 성립 시 수신
   ROUTER가 트래픽 왕복 없이 상대 rid를 즉시 학습한다.
2. **인바운드 identity 라우팅 테이블**: route/spot 송신 경로가 아웃바운드
   connect_routing_id 테이블뿐 아니라 **인바운드로 학습된 peer identity**로도
   rid-addressed 송신을 라우팅한다.
3. **disconnect 정리**: 링크 단절 이벤트에서 해당 rid의 인바운드 항목을 제거
   (half-open 방지). 재연결은 initiator 소유 — non-initiator 방향 송신의
   일시 실패는 송신 측 timeout 내 재시도(기존 채널 retriable 재시도 정책)로 흡수.

## 3. 적용 계획 (확립된 core 트랙 절차)

1. **core**: `socket_auto_connect_target_matches`의 ROUTE_MESH·SPOT_MESH를
   `< 0`으로 변경 + probe 활성화 + 인바운드 identity 라우팅(route bridge·
   spot data plane 송신 경로) + **회귀 테스트**(단방향 페어에서 양방향
   rid-addressed 송수신, disconnect 후 테이블 정리, 재연결 후 재학습).
2. **버전 8.6.0** — 3곳 동기(루트 VERSION, core/include/zlink.h,
   core/packaging/conan/conandata.yml) → `core/v8.6.0` 태그 → GitHub Actions
   릴리즈(gh run watch 감시).
3. **bindings**: `bindings/update_zlink_libs.sh core/v8.6.0 --expect-version 8.6.0`.
4. **framework 4언어 정합**: location auto-connect 소비 로직(dotnet
   `ZLinkLocationAutoConnectHost`, cpp `location_auto_connect_host_service`,
   node·java 대응)을 동일한 `< 0` 단방향으로 + 인바운드 rid 송신 경로 검증.
   공통 draft(framework-location-resolver-store-porting-*.ko.md)의 연결 규칙
   문구 갱신.
5. **재검증**: 4언어 TA e2e·TicTacToe·DeliveryDispatch·(Bingo) 러너 —
   route/spot mesh를 실제로 넘나드는 시나리오 전부.

## 4. 비고

- CLIENT_SERVER·FANOUT·DEALER_MESH는 변경 없음.
- 이 변경 전까지 framework 구현이 route mesh에 임의 initiator 판정(한쪽만
  dial)을 넣어 core 정본(양방향)과 어긋나 있었다 — 본 결정으로 "단방향"이
  정본이 되므로 framework 쪽은 core 릴리즈 후 같은 규칙으로 정합한다.
