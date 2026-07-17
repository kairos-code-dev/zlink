# Codex 문서 독립 리뷰 — S3 iteration 21

## 실행 증거

- provider: OpenAI
- reviewer: Codex agent
- frozen HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- scope: 205개 파일
- 결과: finding이 있으므로 iteration 21 무효

## Finding

[1차소스][high] framework/doc/framework/spec/90-implementation-gap.ko.md:72 — 10.0.0의 통합 RouteMesh 등록 목표와 현재 다섯 언어 구현의 차이가 공통·언어별 gap에서 빠졌다 — exact interface는 `AddRouteMesh`·`addRouteMesh`·`add_route_mesh(meshName)`가 MeshNode builder를 반환하고 ChannelName, handler, client, manual peer, Spot과 Actor 등록을 같은 owner 아래 두도록 고정하지만 현재 source는 기존 client-server channel, route-mesh channel, SpotMesh builder와 production in-memory location helper를 유지한다 — §12.33과 다섯 언어 ID를 추가하고 S8·S9 source·package·sample·E2E 전환 작업을 추적한다.

[1차소스][high] framework/doc/framework/spec/90-implementation-gap.ko.md:548 — C++ TLS gap이 exact interface보다 더 넓은 공개 표면을 요구한다 — exact interface에서 client 인증서 요구 설정은 `stream_node_options_builder_t`가 소유하고 low-level `stream_builder_t`는 `bind`와 `register_session`만 제공하지만, gap은 두 builder 모두 bool overload가 필요한 것처럼 서술한다 — options builder에 bool을 추가하고 registration/runtime/Core option으로 전달하며 low-level TLS 공개 메서드는 제거하는 작업으로 바로잡는다.

[원칙][medium] framework/doc/framework/spec/90-implementation-gap.ko.md:581 — §12.32 제목은 전 언어 결함을 미구현으로 분류하고 해결안은 공통 §9에 없는 raw/binary 예외를 추가한다 — 모든 구현에 decode 표면이 있으나 wire content-type과 codec 일치 검증이 잘못된 동작 결함이고, 공통 계약은 등록 codec 없는 명시적 non-JSON content-type을 실패시키도록 고정한다 — 제목을 결함으로 바꾸고 JSON 또는 등록 codec만 허용하도록 정정한다.
