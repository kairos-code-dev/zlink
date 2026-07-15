# .NET LocationMessaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | 구현 | location store 자동 연결 request marker가 있고, `IZLinkLocationRuntimeQuery.ListPeerLocationsAsync`(raw row)와 `IZLinkPeerLocationResolver.ListLivePeersAsync(...)`(member peer)로 두 provider row가 살아 있음을 확인한다. |
| RM-A2 | 구현 | 수동 endpoint request marker가 있다. |
| RM-A4 | 구현 | 같은 rid failover marker가 있고, runtime query peer list가 rid `api-a`의 살아 있는 row 하나를 교체 endpoint로 보여줄 때까지 대기한 뒤 검증한다. |
| RM-A6 | 구현 | profile/workflow channel을 같은 location store(같은 key prefix)에 등록하고, mesh name filter로 peer row 집합이 섞이지 않음과 각 channel request가 자기 provider evidence에만 남는지 검증한다. |
| RM-B1 | 구현 | scale-out marker가 있고, runtime query peer list에 신규 provider row가 반영될 때까지 대기한 뒤 검증한다. |
| RM-B2 | 구현 | scale-in / graceful drain marker가 있고, 정상 종료된 provider의 peer row가 runtime query peer list에서 제거될 때까지 대기한 뒤 검증한다. |
| RM-B3 | 구현 | provider A를 강제 종료한 직후부터 stale row가 owner lease 만료로 제거될 때까지 무지정 request를 계속 보내며, A row가 남아 있는 구간에도 B가 신규 요청을 처리한 증거와 bounded public error만 발생함을 확인한다. |
| RM-C1 | 구현 | request / send happy path marker가 있다. |
| RM-C2 | 구현 | targeted request by rid marker가 있다. |
| RM-C3 | 구현 | 다중 provider 분산 marker가 있다. |
| RM-C4 | 구현 | timeout 뒤 late reply 비오염 marker가 있다. |
| RM-C5 | 구현 | 미등록 packet 처리 marker가 있다. |
| RM-C7 | 구현 | build-time weight 75/25 provider를 location store 자동 연결로 붙이고 high-weight provider가 더 많이 처리하는 marker가 있다. |
| RM-C8 | 구현 | 1B, 4KiB, 256KiB, 1MiB payload 왕복 hash/length marker를 확인하고, server socket `MaxMessageSize`를 넘는 payload가 실패한 뒤 정상 request가 다시 성공하는지 검증한다. |
| RM-C9 | 구현 | 느린 provider에 다량 one-way send를 제출하고, public bounded-failure oracle 없이 backlog 해소 뒤 후속 request와 evidence가 회복되는지 검증한다. |
