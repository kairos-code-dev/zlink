# Java SpotService E2E feature map

이 디렉터리는 Config 2의 Java framework 검증이다. 실행 코드는 public Spring starter,
`ZLinkSpotManager`, `ZLinkSpotOutbound`, client/server channel builder, route mesh channel
builder, SpotNode builder만 사용한다.

## 이번 E2E에서 구현하는 범위

- SM-A1 entry/user spot 생성 경로는 public `ZLinkSpotManager.getOrCreate`로 user spot을
  생성하고 evidence로 확인한다.
- SM-A2 user spot request와 state mutation은 public `ZLinkSpotOutbound.requestToSpot`으로
  검증한다.
- SM-A3/SM-A4 route resolver와 owner routing은 다음 확장 묶음에서 target owner 분산으로
  검증한다. 현재 baseline은 client/server egress로 `room-a`의 외부→spot 경로를 고정한다.
- SM-C1 request/send/timeout은 외부 consumer의 public `ZLinkSpotOutbound`로 검증한다.
- SM-C1의 미등록 packet negative path는 dispatch error observer와 client exception으로
  확인한다.
- SM-F1은 Java public `ClientServerChannelBuilder.enableSpotRouteEgress`와
  `ZLinkSpotNodeBuilder.acceptSpotRoutesFromChannel`을 사용하는 현재 공개 API로 검증한다.
- SM-F2 route mesh egress는 public API가 존재하지만 이번 baseline runner에서는 아직 통과
  대상으로 고정하지 않았다.

## Java public API 차이로 부분 보류

- SM-B8: Java public API는 `ZLinkEntrySpotContext.destroyActor(ZLinkActor)` 형태다. 문서의
  id 기반 `DestroyActorAsync(actorId)` 표면은 Java에 없으므로 동일 절차로 작성하지 않는다.
- SM-B 계열 actor join/packet 시나리오는 이후 SpotService 확장 묶음에서 별도 구현한다.
- session/stream bound-session 시나리오는 이후 SpotService 확장 묶음에서 별도 구현한다.
