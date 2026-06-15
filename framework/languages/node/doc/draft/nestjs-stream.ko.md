<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS SPOT](./nestjs-spot.ko.md) | [다음: Draft -- ZLink Framework Node.js SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [정식 STREAM spec](../spec/nestjs-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- ZLink Framework NestJS STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 구현 기준은 [정식 STREAM spec](../spec/nestjs-stream.ko.md)이다.
> STREAM public session은 header 기반 단일 `onDispatch(header, payload)` 표면을 쓴다.

## 1. 기준 표면

```ts
export interface ZLinkSession {
  readonly context: ZLinkSessionContext;

  onConnected(signal?: AbortSignal): Promise<void>;
  onDisconnected(signal?: AbortSignal): Promise<void>;
  onError(error: ZLinkStreamError, signal?: AbortSignal): Promise<void>;

  onDispatch(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void>;
}
```

raw session public type은 채택하지 않는다. raw stream write는 `ZLinkStream.write(...)`
하나로 제한한다.

## 2. 회귀 테스트

이 draft는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| header session node | `onDispatch(header, payload)`가 호출된다. |
| 같은 node에 session 중복 등록 | 한 node에 session type을 중복 등록하면 startup validation 오류다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS SPOT](./nestjs-spot.ko.md) | [다음: Draft -- ZLink Framework Node.js SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
