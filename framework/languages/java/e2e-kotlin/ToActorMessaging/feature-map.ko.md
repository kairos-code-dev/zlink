# ToActorMessaging feature-map

이 디렉토리는 공통 config-9 `to-actor messaging` 문서의 Kotlin 구현 gap을 기록한다.

2026-07-07 기준 Kotlin framework에는 `ZLinkActorClient` coroutine extension이 있지만,
`framework/languages/java/e2e-kotlin/ToActorMessaging` 실행 스위트는 아직 없다. 따라서 Kotlin은
공통 config-9의 구현 완료로 판정하지 않는다.

남은 작업:

- Java `framework/languages/java/e2e/ToActorMessaging` 스위트를 Kotlin Gradle 구조로 포팅한다.
- actor owner 서버, caller 서버, client runner를 Kotlin 코드로 작성한다.
- `sendToActorAwait`와 `requestToActorAwait` public extension을 사용해 `/send`, `/request` endpoint를 검증한다.
- `run_e2e.sh`가 Redis를 준비하고 `to-actor-messaging e2e result=passed`를 출력하도록 만든다.
