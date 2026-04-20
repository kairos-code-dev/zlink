[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework Spring Boot SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- `SpotNode` bean 생성
- `SPOT` discovery attach
- publish/subscribe handler bean 등록
- 필요할 때 spot-to-spot routed 호출 허용

## 2. 기본 등록

```java
@Configuration
public class SpotConfig {
    @Bean
    ZLinkSpotNodeCustomizer playSpotNode() {
        return options -> {
            options.setSpotNodeName("play");
            options.useDiscovery(registry -> registry.add("tcp://registry1:5551"));
        };
    }
}
```

## 3. Public surface

- current channel publish/subscribe
- attach된 channel client를 통한 다른 channel send/request
- 필요할 때만 `spot-to-spot` routed send/request

즉 high-level `SPOT` 표면은 `rid` 직접 지정보다 current channel publish와
cross-channel client를 먼저 설명하는 편이 맞다.

## 4. Spot-to-spot

spot-to-spot routed 호출은 남긴다. 다만 일반 channel messaging과 섞지 않는다.

```java
spotClient.requestToAsync(targetRid, targetSpotRid, request, options);
```
