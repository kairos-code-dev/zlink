<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ShoppingMall Sample](./shoppingmall-sample.ko.md) | [다음: SPOT 샘플](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[Java 묶음](../../README.ko.md) | [SPOT](../../spec/spring-boot-spot.ko.md) | [Actor/Session](../../spec/spring-boot-actor-session.ko.md) | [STREAM](../../spec/spring-boot-stream.ko.md)

# GameQuest Sample (Java)

> 언어 중립 시나리오 정본은 [공통 샘플 — GameQuest](../../../../../doc/spec/sample/event/gamequest.ko.md)다.
> 실행 코드는 `samples/java/GameQuest`(Java)에 있다. Kotlin 미러(`samples/kotlin/GameQuest`)는
> 아직 포팅되지 않았다.

## 1. 목적

stateless한 `GameApi` 서버와 stateful한 `QuestMission` 서버를 분리하고, ZLink fanout과 event
sourcing으로 quest 진행을 처리하는 gameplay 샘플이다. client action은 여러 `GameApi`
instance 중 어디로 들어와도 되고, gameplay event는 fanout으로 흘러 `PlayerQuestSpot`이
`playerId` owner 기준으로 처리한다. quest 진행은 과거 event의 누적으로 결정되고, projection이
사라져도 quest event stream replay로 재계산된다. payload codec은 JSON이다.

## 2. 서버 구성

`Registry`, `GameApi`(x2: `api-a`/`api-b`), `QuestMission`(x2: `mission-a`/`mission-b`),
`Client`. `GameApi`는 action을 받는 client/server channel handler(kill/collect/complete/
enter/unlock 등), client session을 받는 STREAM node, gameplay event를 내보내는 fanout
publisher를 가진 stateless 서버다. `QuestMission`은 fanout event를 구독하고 `playerId` owner로
route된 `PlayerQuestSpot`이 `QuestEventStore`에서 `(playerId, questId)` event stream을 replay해
quest aggregate를 복원하고, 조건 평가·progress/completion/reward event append·
`QuestReadModelStore` projection 갱신을 소유한다.

## 3. 전체 흐름

1. client가 `GameApi` STREAM에 접속해 `SubscribeQuestReq`로 quest notify를 구독한다.
2. client action(`KillMonsterReq` 등)을 `GameApi` action channel로 보내면 `GameApi`가
   gameplay event를 fanout으로 publish한다(같은 `idempotencyKey`는 같은 event로 dedupe).
3. event는 `playerId` owner index(`= UTF-8 byte 합 % 2`)에 따라 `QuestMission` instance의
   `PlayerQuestSpot`에 route된다.
4. spot이 quest 조건을 평가해 progress/completion/reward domain event를 append하고 projection을
   갱신한 뒤, 완료 시 client의 bound session으로 `QuestCompletedNotify`를 push한다.
5. projection을 삭제해도 event stream replay로 동일하게 재생성되고, 외부 fanout 누락은
   gameplay snapshot 재동기화로 보정한다.

## 4. 비동기 진행 관용구

client는 stream connector의 `await(...)`/`request(...).await(Class)` 와 `CompletionStage`로
진행하고, server assertion 대기는 `LockSupport.parkNanos` polling을 쓴다. 금지 패턴
(Thread.sleep / while(true) / CountDownLatch)은 쓰지 않는다.

## 5. 경계 메모

dotnet 행위 정본은 `samples/dotnet/.../GameQuest`다. dotnet은 `GameApi`의 action API를
HTTP(ASP.NET) + WebSocket으로 노출하지만, Java 샘플은 같은 시나리오를 JSON-codec ZLink
client/server channel + STREAM session으로 옮긴다(게이트가 HTTP를 요구하지 않음). owner
routing은 `playerId` 기준으로 안정적이고, event-sourced quest 상태는 spot이 소유한다.

## 6. Client self-check

`FirstHunt`(monster 3킬 → reward), `OpenAuction`(feature unlock → reward + snapshot 확인),
idempotency(중복 kill이 같은 `eventId` 반환), projection rebuild(삭제 후 replay로 reward 복원),
`HerbGathering`(healing-herb 수집 → reward), gameplay snapshot 재동기화(publish 누락 event를
sync로 보정)를 client가 검증하고, 최종적으로 server-side assertion으로 event append·projection
일관성을 확인한다.

## 7. 완료 기준

- event sourcing·fanout owner routing·projection rebuild·idempotency·snapshot 재동기화가 모두
  동작한다.
- JSON codec을 쓰고 금지 패턴을 쓰지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 호출부에 노출되지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: ShoppingMall Sample](./shoppingmall-sample.ko.md) | [다음: SPOT 샘플](./spot-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
