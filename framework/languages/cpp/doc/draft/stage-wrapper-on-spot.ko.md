<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ SPOT Samples](./spot-samples.ko.md) | [다음: Draft -- C++ STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [SPOT](./cpp-spot.ko.md)

# Draft -- C++ Stage Wrapper On SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한
> 조건을 정리한다.

`C++`에서는 stage wrapper가 특히 중요하다.

- 현재 `SpotRid`, `NodeRid` 노출
- packet registry
- timer registration
- outbound channel client 접근
- state 와 domain method 보관

stage wrapper는 게임 기능을 직접 뜻하지 않는다.
그 위에 다른 응용이 올라가도 쓸 수 있는 host/runtime 패턴으로 설명하는 편이 맞다.
