[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Scope](./implementation-scope-and-nongoals.ko.md)

# Draft -- ZLink Framework .NET Backend Dependency Policy

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework가 현재 `bindings/dotnet`을 backend로
> 사용하면서도 나중에 저수준 라이브러리를 교체할 수 있게 하기 위한 기준을 정리한다.

## 1. 목적

현재 구현은 `bindings/dotnet` 라이브러리를 backend로 사용하는 것이 가장 현실적이다.
다만 framework public contract가 backend 구현체에 과하게 묶이면, 나중에 저수준
라이브러리를 교체할 때 public API까지 같이 깨지게 된다.

이 문서는 아래 둘을 동시에 만족시키는 기준을 정한다.

- 지금은 `bindings/dotnet`을 이용해서 구현한다.
- 나중에는 다른 저수준 `.NET` 라이브러리로 backend를 바꿀 수 있게 한다.

## 2. 기본 원칙

- framework public contract가 우선이고, backend 라이브러리는 교체 가능한 구현체다.
- public API에 backend 서비스 객체를 직접 노출하지 않는다.
- backend 교체 시 바뀌기 쉬운 타입은 framework 경계 안에 숨긴다.
- 이미 public contract에 남겨 둔 transport primitive는 "backend와 독립된 의미를 가진
  핵심 값"으로 본다.

## 3. 현재 backend 정책

- 현재 구현 backend는 `bindings/dotnet`이다.
- framework runtime은 내부에서 `Discovery`, `DealerSocket`, `RouterSocket`,
  `SpotNode`, `Spot`, `Registry` 같은 하부 객체를 사용할 수 있다.
- 하지만 framework 사용자는 이 객체를 constructor parameter나 public property로
  직접 받지 않는 편을 기본으로 본다.

즉 "지금은 `bindings/dotnet`을 써서 구현한다"와 "framework public API가 곧바로
`bindings/dotnet` 객체 모델이어야 한다"는 같은 뜻이 아니다.

## 4. Public API에 남겨도 되는 것

현재 문서 기준에서 아래 타입은 public contract에 남겨 둔다.

- `RoutingId`
- `Message`
- `SendFlags`

이 타입들은 특정 runtime 객체가 아니라, transport identity / payload / submit option
같은 기초 primitive로 본다. 즉 나중에 backend를 바꾸더라도 같은 의미를 유지하는
compatibility layer를 제공할 수 있어야 한다.

## 5. Public API에 직접 새어 나오면 안 되는 것

아래 타입이나 객체 모델은 framework public contract에 직접 노출하지 않는다.

- `DealerSocket`
- `RouterSocket`
- `Discovery`
- `SpotNode`
- `Spot`
- `Registry`
- `RegistryQueryClient`
- 하부 timer, recv loop, raw socket monitor 객체

이 객체들은 backend 구현 세부에 가까우므로, public surface에 들어오면 나중 교체가
곧바로 breaking change가 된다.

## 6. 진단/운영 타입 정책

monitoring, registry query, spot status는 하부와 가까운 값이 일부 public surface에
남을 수 있다. 이때 기준은 아래와 같다.

- source 이름, timestamp, logical event kind는 framework 소유 의미로 본다.
- native enum이나 raw status 값은 optional diagnostic detail로만 남긴다.
- backend 교체 시 동일 의미를 유지할 수 없으면, framework 쪽 synthetic enum과
  snapshot DTO를 우선하고 native detail은 줄이는 편을 기본으로 본다.

즉 monitoring public API는 "backend raw event를 그대로 재수출"하는 구조보다,
"framework가 재해석한 typed runtime event + 필요할 때만 native detail" 구조가
더 안전하다.

## 7. 구현 지침

- framework 내부에는 backend adapter layer를 둔다.
- registration, lifecycle, monitoring, query는 framework service가 맡고, backend
  호출은 adapter layer가 맡는다.
- 샘플 문서가 low-level binding 타입을 직접 보여 주더라도, framework public surface
  설명과 섞이지 않게 분리한다.

## 8. 교체 시 규칙

나중에 저수준 라이브러리를 교체할 때는 아래 순서를 따른다.

1. framework public contract를 먼저 유지한다.
2. 기존 backend adapter와 새 backend adapter를 같은 contract 뒤에 붙인다.
3. public API에 남아 있는 primitive가 새 backend에서 그대로 유지 가능한지 확인한다.
4. 유지 불가능한 타입이 있으면, backend 교체와 같은 변경에서 바로 없애지 말고
   먼저 framework wrapper를 도입한 뒤 교체한다.

즉 backend 교체는 adapter layer 교체가 기본이고, public API 교체는 별도 breaking
change 작업으로 분리하는 편을 원칙으로 본다.
