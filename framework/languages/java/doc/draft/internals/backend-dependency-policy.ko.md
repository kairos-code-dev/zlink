<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [표면 매핑](./dotnet-to-java-surface-mapping.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java Backend Dependency Policy

> 이 문서는 **구현 전 초안**이다.
> Java framework가 `bindings/java`에 의존하는 방식을 정리한다. 기준은 `.NET`
> framework의 backend adapter 구조다.

## 1. 목적

Java framework는 지금 `bindings/java`를 backend로 사용한다. 그러나 public API가
binding 구현 객체에 얽히면 binding 내부 구조가 바뀔 때 framework 사용자 코드까지
깨진다. 따라서 framework와 binding 사이에는 adapter layer를 둔다.

## 2. 원칙

- framework public contract가 우선이다.
- public API에 binding concrete type을 직접 노출하지 않는다.
- native handle, socket object, monitor loop, registry runtime object는 adapter
  안에 숨긴다.
- `RoutingId`, `Message`, `SendFlags`처럼 언어 중립 의미가 있는 값 타입만 public
  contract에 남길 수 있다.
- framework는 Java binding의 public API만 호출한다.

## 3. Adapter 계약

backend 의존은 `Runtime/Backend/Contracts/` 의 port interface 한 묶음으로
격리한다. 이 port를 `bindings/java` 위에 구현한 어댑터가
`Runtime/Backend/DotNet/`(.NET) 의 Java 대응물이다. `.NET` 의
`IZLinkBackendAdapterFactory` 와 5개 adapter port를 그대로 옮긴다(`I` prefix는
떼고 `ZLink` prefix는 유지).

| port interface (`Runtime/Backend/Contracts/`) | 역할 | Java backend 구현 대상 (`bindings/java`) |
|-----------------------------------------------|------|------------------------------------------|
| `ZLinkBackendAdapterFactory` | 정확히 5개 adapter를 만드는 factory | `ZLinkJavaBackendAdapterFactory` |
| `ZLinkChannelBackendAdapter` | dealer/router/publisher/subscriber socket wrapping, send/request, receive pump | dealer/router/publisher/subscriber socket |
| `ZLinkSpotBackendAdapter` | `SpotNode`/`Spot`/timer wrapping | spotNode/spot/timer |
| `ZLinkStreamBackendAdapter` | stream socket wrapping, session attach, frame send/reply | stream socket |
| `ZLinkRegistryBackendAdapter` | embedded `Registry`/`RegistryQueryClient` wrapping | registry/registryQueryClient |
| `ZLinkMonitoringBackendAdapter` | socket/discovery/registry/spot event source를 framework typed event로 변환 | socket monitor/discovery 이벤트 |

factory는 `createChannelAdapter`, `createSpotAdapter`, `createStreamAdapter`,
`createRegistryAdapter`, `createMonitoringAdapter` 5개 메서드만 노출한다.

### 3.1 Java binding wrapper 목록

`.NET` 의 `Runtime/Backend/DotNet/Wrappers/` 에는 binding 객체를 framework 안쪽으로
숨기는 wrapper들이 있다. Java adapter는 같은 wrapper들을 `bindings/java` 위에
1:1로 구현해야 한다.

- context wrapper
- dealer socket wrapper
- router socket wrapper
- publisher socket wrapper
- subscriber socket wrapper
- discovery wrapper
- spotNode wrapper
- spot wrapper
- stream socket wrapper
- registry wrapper
- registryQueryClient wrapper
- socket monitor wrapper

이 wrapper들의 생성은 오직 adapter 내부에서만 일어난다. wrapper가 감싸는 binding
concrete type은 public surface에 새어 나오지 않는다.

adapter는 framework internal package에 둔다. 사용자 guide와 sample은 adapter 타입을
직접 보여 주지 않는다.

> 참고: port의 정확한 시그니처는 `framework/languages/dotnet/src/Zlink.Framework/
> Runtime/Backend/Contracts/*.cs` 를 코드로 읽어 Java adapter 구현 가이드에 옮긴다.
> backend 의존 정책의 정식 기준은 `.NET` 코드다. 이 절은 Node
> [backend-dependency-policy §7](../../../../node/doc/internals/backend-dependency-policy.ko.md)
> 의 어댑터 구성 규칙을 Java/Spring 표면으로 옮긴 것이다.

## 4. Public API에 새면 안 되는 것

허용 primitive(`RoutingId`, `Message`, `SendFlags`)를 제외하고 아래 binding
concrete type과 객체 모델은 framework public contract에 직접 노출하지 않는다.

- binding `Context`
- dealer/router/publisher/subscriber socket, stream socket
- binding `Discovery`
- binding `SpotNode`, `Spot`
- binding `Registry`, `RegistryQueryClient`
- native receive loop, monitor handle, timer handle
- internal frame encoder/decoder concrete type

필요한 diagnostic 값은 framework DTO로 재해석해서 노출한다. native enum이나 raw
status는 꼭 필요한 경우 optional detail로만 둔다.

## 5. 검증 기준

| 테스트 | 확인 기준 |
|--------|-----------|
| public surface backend leakage | 허용한 값 타입을 제외하고 binding concrete type이 public API에 없다 |
| backend factory wrappers | factory가 channel, spot, stream, registry, monitoring adapter 5종을 모두 만들어 내고, wrapper 생성이 adapter 내부에 머문다 |
| no reflection bypass | framework 코드가 binding non-public member를 reflection으로 호출하지 않는다 |
| adapter-only native construction | native object 생성은 adapter 내부에서만 일어난다 |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [표면 매핑](./dotnet-to-java-surface-mapping.ko.md)
<!-- framework-adapter-nav:bottom:end -->
