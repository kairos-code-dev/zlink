# .NET LocationMessaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | 구현 | store 자동 연결 request + `ListMeshNodeDescriptorsAsync` 두 descriptor + `Snapshot(meshName)` ready peer 검증 marker가 있다(run_e2e.sh 전량 그린 실측). |
| RM-A2 | 구현 | 수동 endpoint request marker가 있다. |
| RM-A4 | 구현 | 같은 RID 교체 전·후 descriptor generation/endpoint와 ready peer 전환 marker가 있다. |
| RM-A6 | 구현 | MeshName 별 descriptor 집합·runtime snapshot 분리 marker가 있다. |
| RM-B1 | 구현 | scale-out 뒤 신규 descriptor·ready ChannelName member 대기 marker가 있다. |
| RM-B2 | 구현 | 정상 종료 provider의 descriptor·ready member 제거 대기 marker가 있다. |
| RM-B3 | 구현 | owner lease 만료 후 stale descriptor 제외와 생존 provider 성공/bounded error marker가 있다. |
| RM-C1 | 구현 | request / send happy path marker가 있다. |
| RM-C2 | 구현 | targeted request by rid marker가 있다. |
| RM-C3 | 구현 | 다중 provider 분산 marker가 있다. |
| RM-C4 | 구현 | timeout 뒤 late reply 비오염 marker가 있다. |
| RM-C5 | 구현 | 미등록 packet 처리 marker가 있다. |
| RM-C7 | 구현 | `SetWeight`·descriptor `ChannelWeights`·runtime `Weight` 기반 75/25 분포 marker가 있다. |
| RM-C8 | 구현 | 1B, 4KiB, 256KiB, 1MiB payload 왕복 hash/length marker를 확인하고, server socket `MaxMessageSize`를 넘는 payload가 실패한 뒤 정상 request가 다시 성공하는지 검증한다. |
| RM-C9 | 구현 | non-blocking 즉시 backpressure vs blocking bounded admission 대조와 회복 marker가 있다. |
