[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [SPOT](./nestjs-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md)

# Draft -- Node.js Stage Wrapper On SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한
> 조건을 정리한다.

`Stage` wrapper는 아래 역할을 가져야 한다.

- 현재 `SpotRid`, `NodeRid` 노출
- packet handler registration
- timer registration
- outbound channel client 접근

상태와 도메인 메서드는 stage wrapper에 두고, packet handler와 외부 channel 호출은
별도 provider로 분리하는 편이 맞다.
