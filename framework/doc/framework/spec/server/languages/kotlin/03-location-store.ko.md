# Kotlin Location Store·Redis 공개 인터페이스

[Kotlin 계약 목차](README.ko.md) · [Java Location Store·Redis](../java/03-location-store.ko.md) ·
[Java routing ID 자동 할당](../java/04-routing-id-allocation.ko.md)

Kotlin은 Java의 store-neutral record, `ZLinkLocationStore`, `ZLinkActorTransferStore`와 공식
`ZLinkRedisLocationStore`를 그대로 사용한다. 동일한 타입을 Kotlin package에 다시 선언하지 않는다.
Root 등록 경계는 다음 호출로 고정한다.

```kotlin
val redisOptions = ZLinkRedisLocationOptions()
    .connectionString("redis-host:6379") // 공식 Redis extension의 연결 정보를 설정한다.
    .keyPrefix("zlink:game")             // 다른 배포와 key namespace를 분리한다.

options.addLocationStore(ZLinkRedisLocationStore(redisOptions))
options.configureLocations().apply {
    setHeartbeatInterval(Duration.ofSeconds(10)) // owner lease 갱신 주기를 설정한다.
    setOwnerLeaseTtl(Duration.ofSeconds(30))      // owner lease 유효 기간을 설정한다.
}
```

Kotlin은 Java `ZLinkLocationOptions`의 여섯 `Duration` option을 그대로 사용한다. 기본값은 heartbeat 10초,
owner lease TTL 30초, polling 1초, store failure grace 30초, routing ID fencing margin 5초와 owner lease
renew timeout 3초다. 모든 값은 양수여야 한다. Routing ID 자동 할당을 사용하면
`heartbeatInterval + ownerLeaseRenewTimeout < ownerLeaseTtl - routingIdFencingMargin`도 만족해야 한다.
정확한 Java 시그니처와 검증 시점은
[Java Location Store §1](../java/03-location-store.ko.md#1-root-등록과-option)을 따른다.

자동 discovery, remote Spot·Actor 위치, routing ID 자동 할당 또는 Actor transfer를 구성하면 Java와 같은
startup capability validation을 적용한다. Kotlin coroutine adapter가 store operation을 기다리더라도
participant set, Actor generation, membership epoch와 recovery lease의 원자 비교 의미를 바꾸지 않는다.
