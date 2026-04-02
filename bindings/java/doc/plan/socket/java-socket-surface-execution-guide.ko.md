# Java Socket Surface Split 실행 가이드

> 상태: 완료
> 대상 범위: `bindings/java/src/main/java/dev/kairoscode/zlink/**`, `bindings/java/src/test/java/dev/kairoscode/zlink/**`, `bindings/java/plan/socket/**`
> 보조 참조 스펙: [`2026-03-27-java-socket-surface-detailed-design.ko.md`](/home/hep7/project/kairos/zlink/bindings/java/plan/socket/2026-03-27-java-socket-surface-detailed-design.ko.md)
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 목적

이 문서는 Java socket surface split 작업을 끝까지 완료하기 위한 유일한 실행 authority다.

작업 목적:

- giant [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Socket.java)를 Java/POSD 기준으로 분해한다.
- typed socket facade(`PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`, `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`)를 추가한다.
- 기존 성능 경로를 유지하면서 compile-time surface 제한을 강화한다.
- `Poller`, `SocketPollSet`, `MonitorSocket`, `Message`, contract tests를 새 socket surface에 맞춘다.

보조 상세 설계 문서는 background spec으로만 사용한다.
실행 중 실제 판단 기준은 이 가이드 한 파일로 고정한다.

설계 변경이 필요하면:

1. 필요 시 상세 설계 문서를 갱신한다.
2. 반드시 이 가이드에 같은 결정을 흡수한다.
3. 그 다음 코드를 수정한다.

코드가 이 가이드와 맞고 상세 설계 문서가 뒤처진 상태는 허용하지 않는다.

## 2. 적용 원칙

- 실행 authority는 이 가이드 하나로 고정한다.
- 상세 설계 문서는 보조 참조 문서로만 사용한다.
- 구현은 Java 단일 상속 제약을 지키며, public 다중 capability 상속은 만들지 않는다.
- unsupported operation은 runtime 오류보다 class surface 제한으로 먼저 줄인다.
- `Message`는 payload conversion의 유일한 public 진입점으로 유지한다.
- direct `ByteBuffer`, native `MemorySegment`, direct Netty `ByteBuf`, scratch buffer fast path는 유지한다.
- `perf/` 작업은 이번 범위가 아니다.
- 문서, 코드, 테스트 중 하나만 바뀐 상태로 남기지 않는다.

이 가이드에서 고정하는 public surface 핵심:

- final typed socket facade 8종:
  - `PairSocket`
  - `DealerSocket`
  - `RouterSocket`
  - `StreamSocket`
  - `PubSocket`
  - `SubSocket`
  - `XPubSocket`
  - `XSubSocket`
- `Socket`은 abstract common base다.
- `Socket` base에는 공통 lifecycle, option, monitor, TLS만 둔다.
- `Message`만 payload conversion을 담당한다.
- `STREAM`은 bind-only, directed-send-only다.
- discovery attach는 `DealerSocket`, `RouterSocket`, `PubSocket`, `SubSocket`만 지원한다.
- canonical topic/filter 표현은 `String`이다.
- `SubscriptionEntry`는 최종적으로 `String filter` 기반이다.
- `XPubSocket`은 `subscriptionEvent()`만 public으로 노출한다.
- 2026-03-27 기준 core `zlink_set_subscription` / `zlink_unset_subscription`는 `SUB` / `XSUB`만 허용하고 `XPUB`는 `EINVAL`을 반환하므로 XPUB manual mutation facade는 이번 범위에 포함하지 않는다.
- `Poller`, `SocketPollSet`, `MonitorSocket`는 abstract `Socket` base와 계속 호환되어야 한다.

## 3. 금지 규칙

- 새 generic `Socket(Context, SocketType)` 사용 추가 금지
- 새 socket facade에 `byte[]`, `ByteBuffer`, `ByteBuf`, `MemorySegment` direct convenience 추가 금지
- facade마다 별도 scratch arena / scratch buffer 복제 금지
- `Poller` / `SocketPollSet` public signature 확장 금지
- 테스트를 약하게 바꿔 구현 버그를 숨기는 변경 금지
- `sleep`, retry loop, poll-until-success 기반 테스트 추가 금지

## 4. 반복 순서

항상 첫 미완료 slice부터 처리한다.

1. 이 가이드와 현재 코드 표면을 대조한다.
2. 해당 slice의 코드 변경을 끝낸다.
3. 관련 테스트를 추가 또는 갱신한다.
4. 검증 명령을 실행한다.
5. 이 가이드의 작업 레지스터를 갱신한다.
6. 다음 미완료 slice로 넘어간다.

중간에 설계 충돌이 보이면:

1. 이 가이드의 결정부터 먼저 수정한다.
2. 필요하면 상세 설계 문서를 뒤이어 맞춘다.
3. 그 다음 구현을 진행한다.

## 5. 검증 절차

현재 즉시 가능한 smoke:

```bash
cd bindings/java && ./gradlew test --no-daemon

./bindings/java/plan/socket/run_java_socket_surface_execution.sh --max-iterations 0
```

slice별 기본 검증:

- 구조 변경 직후:
```bash
cd bindings/java && ./gradlew compileJava --no-daemon
```

- contract 변경 직후:
```bash
cd bindings/java && ./gradlew test --no-daemon
```

최종 검증:

```bash
cd bindings/java && ./gradlew clean test --no-daemon
cd bindings/java && ./gradlew integrationTest --no-daemon
```

필요 시 추가 확인:

```bash
cd bindings/java && ./gradlew test --no-daemon --tests '*SocketContractTest'
cd bindings/java && ./gradlew test --no-daemon --tests '*SocketSubscriptionContractTest'
cd bindings/java && ./gradlew test --no-daemon --tests '*CallbackModeContractTest'
```

## 6. 로그 / 운영 규칙

- wrapper는 [`core/tools/ralphloop/run_codex_execution_guide_loop.sh`](/home/hep7/project/kairos/zlink/core/tools/ralphloop/run_codex_execution_guide_loop.sh)를 호출한다.
- guide와 master-plan은 같은 파일로 둔다.
- 기본 로그 디렉터리는 `bindings/java/plan/socket/logs/` 이다.
- 병렬 실행이 필요하면 `--logs-dir`, `--gate-label`을 함께 분리한다.
- commit / push는 사용자 요청이 있을 때만 수행한다.

## 7. 작업 레지스터

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### Slice 1. `SocketCore` / plane 분해

상태: `완료`

대상:

- [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Socket.java)
- `SocketCore.java`
- `MessagePlane.java`
- `TopicPlane.java`
- `LegacySocketCompat.java`

작업:

- `Socket`의 common lifecycle/monitor/TLS/option/callback state를 `SocketCore`로 추출
- raw send/recv를 `MessagePlane`으로 추출
- topic publish/subscribe/subscription/subscription-event를 `TopicPlane`으로 추출
- deprecated buffer convenience를 `LegacySocketCompat`로 이동

진행 메모:

- 2026-03-27: `SocketCore`, `MessagePlane`, `TopicPlane`, `LegacySocketCompat` helper 파일을 추가했고 `Socket` public entrypoint를 helper 위임으로 전환했다.
- 2026-03-27: `SubscriptionEntry`를 canonical `String filter` record로 전환했고 XPUB subscription event도 `String filter` 기준으로 노출하기 시작했다.
- 2026-03-27: core `zlink_set_subscription` / `zlink_unset_subscription`가 현재 `XPUB`를 허용하지 않는 것을 확인했다. 가이드의 XPUB manual mutation 항목은 제거하고 `subscriptionEvent()` + dedicated pub option routing만 이번 canonical surface로 고정한다.
- 2026-03-27: deprecated buffer send/recv bridge를 `LegacySocketCompat`에 모아 `MessagePlane`을 canonical message send/recv 경로에 더 가깝게 축소했다.
- 2026-03-27: callback registration/error state와 send scratch ownership을 `SocketCore`로 이동했다. `Socket`은 lifecycle/message/topic public entrypoint와 low-level native primitives에 더 가깝게 줄었고 giant class가 data-plane helper state를 직접 들고 있지 않게 됐다.
- 2026-03-27: `./gradlew compileJava --no-daemon`, `./gradlew test --no-daemon`, `./gradlew integrationTest --no-daemon` 통과.

검증:

```bash
cd bindings/java && ./gradlew compileJava --no-daemon
cd bindings/java && ./gradlew test --no-daemon
```

완료 기준:

- giant `Socket`가 data-plane 세부 구현을 직접 소유하지 않는다
- 기존 테스트 baseline이 유지된다

### Slice 2. typed socket facade 추가

상태: `완료`

대상:

- `PairSocket.java`
- `DealerSocket.java`
- `RouterSocket.java`
- `StreamSocket.java`
- `PubSocket.java`
- `SubSocket.java`
- `XPubSocket.java`
- `XSubSocket.java`
- [`SubscriptionEntry.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/SubscriptionEntry.java)
- `SubscriptionEvent.java`

작업:

- final concrete facade 8종 추가
- facade는 `SocketCore`, `MessagePlane`, `TopicPlane`에 위임
- class별 허용 API만 public으로 노출
- `SubscriptionEntry`를 canonical `String filter` 기반으로 전환

진행 메모:

- 2026-03-27: `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`, `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`를 추가했고 샘플/테스트 호출부를 typed facade 기준으로 전환했다.
- 2026-03-27: `Socket`을 abstract common base로 낮추고 broad lifecycle/message/topic API를 package-private로 내렸다.
- 2026-03-27: `SubscriptionEntry`를 `String filter` record로 전환했고 `SubscriptionEvent`를 추가했다.

검증:

```bash
cd bindings/java && ./gradlew compileJava --no-daemon
cd bindings/java && ./gradlew test --no-daemon
```

완료 기준:

- typed socket만으로 raw/topic 사용 패턴을 설명 가능하다
- public surface에서 subscription/filter canonical type이 `String`으로 정렬된다

### Slice 3. 주변 타입 정리

상태: `완료`

대상:

- [`Poller.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Poller.java)
- [`SocketPollSet.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/SocketPollSet.java)
- [`MonitorSocket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/MonitorSocket.java)
- [`Message.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Message.java)

작업:

- `Poller`, `SocketPollSet`이 abstract `Socket` base와 안정적으로 동작하도록 정리
- `MonitorSocket`이 `Socket.adopt()` 없이 monitor handle을 소유하도록 정리
- `Message.send/recv` legacy helper를 typed facade 기준으로 축소 또는 deprecated 정리

진행 메모:

- 2026-03-27: `MonitorSocket`이 직접 monitor handle을 소유하도록 바꿨고 `Socket.adopt()`를 제거했다.
- 2026-03-27: `SocketPollingContractTest`를 추가해 `Poller`와 `SocketPollSet`이 typed socket을 abstract `Socket` base로 받아 동작함을 검증했다.
- 2026-03-27: `Socket` deprecated buffer send/recv bridge를 `LegacySocketCompat`로 이동해 `MessagePlane`의 legacy surface를 줄였다.
- 2026-03-27: main 코드의 `Message.from*` legacy factory 의존을 canonical `copyOf` / `wrap*` 경로로 치환했고 `Message.trySend/tryRecv(Socket, ...)`도 deprecated 정리로 맞췄다.
- 2026-03-27: `./gradlew test --no-daemon`, `./gradlew integrationTest --no-daemon` 통과.

검증:

```bash
cd bindings/java && ./gradlew test --no-daemon --tests '*MonitorContractTest'
cd bindings/java && ./gradlew test --no-daemon --tests '*SocketContractTest'
```

완료 기준:

- `Poller`, `SocketPollSet`, `MonitorSocket`가 새 socket 구조 뒤에도 흔들리지 않는다
- main 코드에서 `Socket.adopt(...)` 사용이 제거 방향으로 정리된다

### Slice 4. contract test / 문서 전환

상태: `완료`

대상:

- `src/test/java/dev/kairoscode/zlink/**`
- `bindings/java/plan/socket/**`

작업:

- contract tests를 typed socket 기준으로 갱신
- `XPubSocket` subscription event + dedicated pub option contract 추가
- `RouterSocket` own routing-id, TLS dedicated API smoke 추가
- 설계 문서와 실행 가이드 상태 동기화

진행 메모:

- 2026-03-27: contract tests와 Java samples의 canonical path를 typed socket 기준으로 전환했다.
- 2026-03-27: `SocketContractTest`에 `RouterSocket` own routing-id + TLS dedicated API smoke를 추가했다.
- 2026-03-27: core `zlink_set_subscription` / `zlink_unset_subscription`가 현재 `XPUB`를 지원하지 않는 사실을 반영해 `XPubSocket` contract를 `subscriptionEvent()`와 dedicated pub option smoke로 재정의했다.
- 2026-03-27: 상세 설계 문서에 public canonical surface와 package-private delegation 경계 설명을 보강했고 실행 가이드 상태와 동기화했다.
- 2026-03-27: `./gradlew test --no-daemon`, `./gradlew integrationTest --no-daemon` 통과.

검증:

```bash
cd bindings/java && ./gradlew test --no-daemon
```

완료 기준:

- 테스트가 generic `Socket` broad surface를 canonical path로 가정하지 않는다
- 설계 문서, 실행 가이드, 코드, 테스트가 같은 surface를 설명한다

### Slice 5. generic `Socket` 제거

상태: `완료`

대상:

- [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Socket.java)
- generic `Socket` 호출부 전반

작업:

- `Socket`을 abstract common base로 전환
- `Socket(Context, SocketType)` 제거
- broad generic methods 제거
- 남은 compat hook(`Socket.adopt()`, `MonitorSocket.socket()`) 제거 또는 내부 전용화

진행 메모:

- 2026-03-27: `Socket`을 abstract common base로 전환했고 public constructor를 제거했다.
- 2026-03-27: broad generic lifecycle/message/topic surface를 typed facade wrapper 뒤로 내렸고 main/test/sample에서 `new Socket(ctx, SocketType.X)` 호출을 제거했다.
- 2026-03-27: `Socket.adopt()`와 `MonitorSocket.socket()` accessor를 제거했다.

검증:

```bash
cd bindings/java && ./gradlew clean test --no-daemon
cd bindings/java && ./gradlew integrationTest --no-daemon
```

완료 기준:

- main/test/sample 코드에서 `new Socket(ctx, SocketType.X)` 사용이 `0건`
- main 코드에서 `Socket.adopt(...)` 사용이 `0건`
- final public surface가 상세 설계 문서와 일치한다

### Slice 6. POSD 후속 리팩토링

상태: `완료`

대상:

- `bindings/java/src/main/java/dev/kairoscode/zlink/**`
- `bindings/java/src/test/java/dev/kairoscode/zlink/**`
- `bindings/java/plan/socket/**`

작업:

- socket split 이후에도 남아 있는 shallow wrapper, hidden coupling, change amplification 지점 재점검
- lifecycle / callback / buffer / ownership 정책이 여전히 분산돼 있으면 더 깊은 공통 모듈로 재흡수
- 설명하기 어려운 public 메서드 shape, 중복 facade, hidden contract를 추가 정리
- 성능 또는 core 공개 표면 제약 때문에 유지되는 복잡성만 마지막에 남기고, 나머지는 제거

반복 규칙:

- POSD 관점에서 설명 가능한 리팩토링 대상이 하나라도 남아 있으면 계속 진행한다.
- 새 리팩토링 대상이 발견되면 Slice 6은 계속 `진행중` 상태를 유지한다.
- 더 이상 정리할 대상이 없고 남은 복잡성이 모두 의도적 제약으로 설명 가능할 때만 `완료`로 바꾼다.

진행 메모:

- 2026-03-27: `SocketCore`로 callback/scratch 공통 상태를 더 흡수하고 `Message` legacy helper 사용을 main 경로에서 걷어내면서 Slice 1~3 완료 기준을 충족시켰다.
- 2026-03-27: [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Socket.java) 에 남아 있던 compat-only stream unsupported stub 군을 제거해 legacy STREAM API가 `PairSocket` 등 다른 facade로 public 상속 누수되지 않게 정리했다.
- 2026-03-27: `SocketCore`, `MessagePlane`, `TopicPlane`, `LegacySocketCompat`가 `Socket`의 package-private 직접 경계를 사용하도록 바꿔 helper 경계용 `*Internal` shim 다발을 제거했다.
- 2026-03-27: `SocketContractTest`에 reflection 기반 surface contract를 추가해 `PairSocket`/`StreamSocket`에 legacy stream 또는 비허용 public 메서드가 다시 노출되지 않음을 고정했다.
- 2026-03-27: `./gradlew compileJava --no-daemon`, `./gradlew test --no-daemon --tests '*SocketContractTest'`, `./gradlew test --no-daemon --tests '*SocketSubscriptionContractTest'`, `./gradlew clean test --no-daemon`, `./gradlew integrationTest --no-daemon` 통과.

검증:

```bash
cd bindings/java && ./gradlew clean test --no-daemon
cd bindings/java && ./gradlew integrationTest --no-daemon
```

완료 기준:

- 더 이상 설명 가능한 POSD 리팩토링 대상이 남아 있지 않다
- 남은 복잡성은 성능 또는 core 공개 표면 제약 때문에 의도적으로 유지된 것뿐이다
- 문서와 테스트가 그 상태를 설명한다

## 8. 종료 조건

아래를 모두 만족하면 종료한다.

- 6개 slice가 모두 `완료`
- 상세 설계 문서와 구현이 일치
- `./gradlew clean test --no-daemon` 통과
- `./gradlew integrationTest --no-daemon` 통과
- POSD 관점에서 추가 리팩토링 대상이 더 이상 없다
- 추가 미적용 항목이 없으면 final message를 정확히 `미적용 사항이 없습니다.` 로 남긴다

추가 구현이 남아 있으면 final message는 정확히 `계속 진행 필요` 로 남긴다.
사용자 판단이 필요한 blocker가 있으면 `사용자 입력 필요:` 로 시작한다.
