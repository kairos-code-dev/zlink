<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ SPOT Samples](../internals/spot-samples.ko.md) | [다음: Draft -- C++ STREAM Decisions](../internals/stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [SPOT](cpp-spot.ko.md)

# Spec -- C++ Stage Wrapper On SPOT

> 이 문서는 `SPOT` 위에 상위 stage 모델을 얹는 **패턴 가이드**다. C++ framework public
> 표면에 별도 stage wrapper 계약 타입/빌더는 없고, 테스트의 지역 예제 패턴으로 존재한다.
> stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한
> 조건을 정리한다.

## 인터페이스 경계

stage wrapper는 framework core contract가 아니라 SPOT 위에 올릴 수 있는 상위 패턴이다.
wrapper public 표면은 `SpotRid`, `NodeRid`, packet registry view, timer option,
outbound channel client, domain state와 method처럼 application이 직접 다루는 개념만
가진다. Spot activation state, timer token, outbound transport, packet dispatcher는
framework runtime 구현에 남긴다.

wrapper가 편의를 위해 SPOT 기능을 감싸더라도 내부 runtime 타입을 public 멤버로 노출하면
안 된다. 필요한 기능은 wrapper option과 method로 다시 표현한다.

`C++`에서는 stage wrapper가 특히 중요하다.

- 현재 `SpotRid`, `NodeRid` 노출
- packet registry
- `add_timer(...)` 기반 server-side timer registration
- outbound channel client 접근
- state 와 domain method 보관

stage wrapper는 게임 기능을 직접 뜻하지 않는다.
그 위에 다른 응용이 올라가도 쓸 수 있는 host/runtime 패턴으로 설명하는 편이 맞다.

wrapper가 timer를 제공할 때도 SPOT timer 계약을 그대로 따라야 한다. 즉 tick metadata,
overrun policy, handler exception monitoring을 숨기지 않고 wrapper 옵션으로 사상한다.
room이나 stage 상태 변경은 core SPOT dispatch boundary 안에서만 처리한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ SPOT Samples](../internals/spot-samples.ko.md) | [다음: Draft -- C++ STREAM Decisions](../internals/stream-open-items.ko.md)
<!-- framework-adapter-nav:bottom:end -->
