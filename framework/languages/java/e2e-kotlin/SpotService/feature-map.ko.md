# Kotlin SpotService E2E feature map

이 디렉터리는 Config 2의 Kotlin framework 검증이다. 실행 코드는 public Spring starter,
`ZLinkSpotManager`, `ZLinkSpotOutbound`, client/server channel builder, route mesh channel
builder, SpotNode builder, Kotlin spot 구현만 사용한다.

## 이번 E2E에서 구현하는 범위

- `SM-A1` entry/user spot 생성 경로는 public `ZLinkSpotManager.getOrCreate`로 user spot을
  생성하고 evidence로 확인한다.
- `SM-A2` user spot request와 state mutation은 public `ZLinkSpotOutbound.requestToSpot`으로
  검증한다.
- `SM-C1` request/send/timeout은 외부 consumer의 public `ZLinkSpotOutbound`로 검증한다.
- `SM-C1`의 미등록 packet negative path는 dispatch error observer와 client exception으로
  확인한다.
- `SM-F1`은 public `enableSpotRouteEgress`와 `acceptSpotRoutesFromChannel`을 사용하는
  현재 공개 API로 검증한다.

## 부분 보류

- `SM-B8`은 Java/Kotlin public API가 id 기반 destroy 표면을 제공하지 않아 문서 절차 그대로
  작성하지 않는다.
- actor join/packet, session/stream bound-session, route mesh egress 확장 시나리오는 이후
  SpotService 확장 묶음에서 별도 구현한다.
