[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework Spring Boot SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- `UseSpotDiscovery(channelName, ...)`에 대응하는 discovery 등록
- `SpotNode` bean 생성과 capability별 등록
- current channel publish/subscribe와 attach된 channel client 경로
- local spot 인스턴스가 없는 외부 노드용 publisher client 경로
- 필요할 때만 spot-to-spot routed 호출 허용

현재 공통 정책 기준으로는 아래를 같이 지켜야 한다.

- `SpotNode`는 channel 이름을 직접 소유하지 않고, attach된 discovery view가 active
  channel 범위를 정한다.
- capability는 `router`, `pub/sub`, attach된 channel client, attach된 spot
  publisher client로 나눠서 설명한다.
- spot factory는 `spotName`과 함께 등록하고, 같은 이름 재등록은 덮어쓰지 않고
  예외로 본다.
- spot 생성은 `spotName` 기준으로 설명하고, 운영 코드가 `spotRid -> spotName`
  매핑을 다시 볼 수 있어야 한다.
- timer는 공용 scheduler보다 spot lifecycle registration 표면으로 두는 편이
  자연스럽다.

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

- `ZLinkSpotManager`
- current channel publish/subscribe
- attach된 channel client를 통한 다른 channel send/request
- local spot 인스턴스가 없는 외부 노드용 `ZLinkSpotPublisherClient`
- 필요할 때만 `spot-to-spot` routed send/request

즉 high-level `SPOT` 표면은 `rid` 직접 지정보다 current channel publish와
cross-channel client를 먼저 설명하는 편이 맞다. 다만 실제 운영 코드가
`spotName`으로 생성하고 `spotRid -> spotName` 매핑을 조회해야 하므로,
`ZLinkSpotManager`도 public surface에 함께 둬야 한다.

## 4. Spot-to-spot

spot-to-spot routed 호출은 남긴다. 다만 일반 channel messaging과 섞지 않고,
advanced surface로 설명한다.

```java
spotClient.requestTo(targetRid, targetSpotRid, request, options);
```
