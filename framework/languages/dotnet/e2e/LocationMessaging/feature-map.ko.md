# .NET LocationMessaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | 10.0.0 전환 대상 | location store 자동 연결 request와 `ListMeshNodesAsync(meshName)`의 두 descriptor, `Snapshot(meshName)`의 ready peer 두 개를 함께 검증해야 한다. 현재 source와 runner는 이 목표를 아직 충족하지 않는다. |
| RM-A2 | 구현 | 수동 endpoint request marker가 있다. |
| RM-A4 | 10.0.0 전환 대상 | 같은 RID의 교체 전·후 descriptor generation과 endpoint, runtime ready peer 전환을 검증한다. |
| RM-A6 | 10.0.0 전환 대상 | 같은 store의 서로 다른 MeshName descriptor 집합과 runtime snapshot이 분리되는지 검증한다. |
| RM-B1 | 10.0.0 전환 대상 | scale-out 뒤 신규 descriptor와 ready ChannelName member가 반영될 때까지 기다린다. |
| RM-B2 | 10.0.0 전환 대상 | 정상 종료된 provider의 descriptor와 ready member가 제거될 때까지 기다린다. |
| RM-B3 | 10.0.0 전환 대상 | provider A의 owner lease 만료 후 stale descriptor가 제외되는 동안 B의 성공 evidence와 bounded public error를 검증한다. |
| RM-C1 | 구현 | request / send happy path marker가 있다. |
| RM-C2 | 구현 | targeted request by rid marker가 있다. |
| RM-C3 | 구현 | 다중 provider 분산 marker가 있다. |
| RM-C4 | 구현 | timeout 뒤 late reply 비오염 marker가 있다. |
| RM-C5 | 구현 | 미등록 packet 처리 marker가 있다. |
| RM-C7 | 10.0.0 전환 대상 | `ChannelName(...).SetWeight(...)`, descriptor `ChannelWeights`와 `Channel(meshName, channelName).Weight`를 사용해 75/25 분포를 검증한다. |
| RM-C8 | 구현 | 1B, 4KiB, 256KiB, 1MiB payload 왕복 hash/length marker를 확인하고, server socket `MaxMessageSize`를 넘는 payload가 실패한 뒤 정상 request가 다시 성공하는지 검증한다. |
| RM-C9 | 10.0.0 전환 대상 | 느린 provider에서 non-blocking send submit의 즉시 backpressure 결과와 blocking submit의 bounded admission 결과를 직접 대조하고, backlog 해소 뒤 후속 request와 evidence가 회복되는지 검증해야 한다. |
