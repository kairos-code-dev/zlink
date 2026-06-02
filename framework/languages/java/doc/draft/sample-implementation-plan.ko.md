<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Java STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md) | [Stream Connector](./stream-connector.ko.md)

# Draft -- Java/Kotlin Sample Implementation Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` sample과 같은 수준의 Java/Kotlin sample을
> 구현하기 위한 기준이다.
> 전체 Java/Kotlin framework 포팅의 구현 순서와 release 기준은
> [implementation-execution-plan](./implementation-execution-plan.ko.md)이다.
> 이 문서는 그중 **Phase 10 samples + release gate**를 자세히 정리한다.

## 1. 목표

Java/Kotlin 포팅 완료 기준에는 sample 구현이 포함된다. framework와 connector가
컴파일되는 것만으로는 완료로 보지 않는다. 사용자가 sample을 실행해서 .NET sample과
같은 topology, 같은 흐름, 같은 실패 방어를 확인할 수 있어야 한다.

다만 이 문서만으로 Java/Kotlin 포팅 완료를 판정하지 않는다. 전체 완료 판정은
[implementation-execution-plan](./implementation-execution-plan.ko.md)의 4축
동등성(구조, 기능, 사용성, sample)과
[regression-test-matrix](./internals/regression-test-matrix.ko.md)의 전체 회귀
테스트를 함께 통과해야 한다. 이 문서는 그중 **sample 축**과 sample release gate를
닫기 위한 실행 기준이다.

## 1.1 선행 조건

sample 구현은 framework 본체 구현을 대신하지 않는다. 아래 항목이 먼저 구현되어
컴파일 가능한 public API와 runtime 의미가 닫혀 있어야 한다.

- `zlink-framework-core`, `zlink-framework-spring-boot-starter`,
  `zlink-stream-connector`, connector codec 모듈, Kotlin wrapper, testkit
- channel, Spot, actor/session, STREAM, registry, monitoring, codec public API 표면
- Spring Boot lifecycle, handler scanner, validation, backend adapter, serial execution
- Java binding public API만 사용하는 framework backend 연결
- [regression-test-matrix](./internals/regression-test-matrix.ko.md)의 unit,
  contract, fake backend, integration-single-process, integration-multi-process gate

선행 조건이 닫히지 않은 상태에서 sample만 통과시키기 위해 임시 store, 임시 packet,
sleep 기반 readiness 우회를 추가하지 않는다. 그런 우회는 sample 성공처럼 보이지만
`.NET` 동등 framework 구현을 증명하지 못한다.

## 1.2 이 문서의 범위와 비범위

이 문서는 아래 항목을 정의한다.

- Java/Kotlin sample 목록과 `.NET` sample 대응 관계
- sample 디렉토리 구조와 실행 entry point
- 각 sample이 보여 주어야 하는 topology와 주요 흐름
- sample release gate가 자동으로 확인해야 하는 항목

이 문서는 아래 항목을 정의하지 않는다.

- Java module/package 정규 이름표
- framework public interface 전체 목록
- Spring Boot adapter, backend adapter, lifecycle, validation 구현 순서
- channel/Spot/actor/stream/registry/monitoring/codec의 전체 회귀 테스트
- Kotlin wrapper와 connector codec의 전체 계약

위 항목은 [implementation-execution-plan](./implementation-execution-plan.ko.md),
[handler-interfaces](./handler-interfaces.ko.md), 각 주제 draft, 그리고
[regression-test-matrix](./internals/regression-test-matrix.ko.md)를 따른다.

## 2. Sample 목록

| Sample | 역할 | 필수 여부 | `.NET` 대응 |
|--------|------|-----------|-------------|
| `TicTacToe` | direct STREAM + SPOT + channel 기본 흐름 | 필수 | `samples/TicTacToe` |
| `TicTacToe.SessionGateway` | session actor dispatch, ActorGateway relay, reconnect | 필수 | `samples/TicTacToe.SessionGateway` |
| `Bingo` | matching room, 4 client connector, Entry Spot admission, timer, bound push | 필수 | `samples/Bingo` |
| `StreamingClient` | connector 단독 사용, send/request/on/dispatch mode | 필수 | **없음 (Java 추가 sample)** |

provenance 주석:

- **`StreamingClient`는 `.NET`에 대응 sample이 없는 Java 추가 sample**이다. connector
  단독 사용성을 보이기 위해 Java 쪽에서 새로 만든다. `.NET` 동등성 비교에서 이
  sample은 "추가"로 표시하고, 4축 동등성 판정의 기준 sample에는 넣지 않는다(필수
  실행 대상에는 포함).
- `.NET`에는 `samples/Bingo(session-gateway)`가 존재한다(WIP). 이 Java 계획에서는
  필수 4개만 우선 구현하고, `Bingo(session-gateway)` 대응 Java sample은 **선택
  항목**으로 둔다(필수 release gate 밖).

## 3. 디렉토리 구조

```text
framework/languages/java/samples/
  README.md
  run_samples.sh
  TicTacToe/
    build.gradle.kts
    README.md
    Client/
    Server/
    Shared/
  TicTacToe.SessionGateway/
    build.gradle.kts
    README.md
    Client/
    Server/Api/
    Server/Play/
    Server/Session/
    Server/Registry/
    Shared/
  Bingo/
    build.gradle.kts
    README.md
    Client/
    Server/Api/
    Server/Play/
    Server/Session/
    Server/Registry/
    Shared/
  StreamingClient/
    build.gradle.kts
    README.md
    Client/
    Server/
    Shared/
```

각 sample은 aggregate build entry point와 `run_sample.sh`를 가진다. `run_samples.sh`는
필수 sample을 모두 실행한다.

## 4. TicTacToe Direct

direct sample은 아래를 보여 준다.

- API server와 Play server 분리
- API server가 Play server channel로 game 생성 요청
- Play server가 game room Spot 생성
- client가 Play server STREAM endpoint에 직접 연결
- header session에서 인증, join, place mark 처리
- room Spot에서 game state 변경과 client push

direct sample은 수동 연결을 사용할 수 있다. 이 범위의 수동 연결 설명은 direct
sample 안으로만 한정한다.

## 5. TicTacToe SessionGateway

SessionGateway sample은 아래를 반드시 보여 준다.

- Registry, Api, Play, primary Session, reconnect Session을 별도 process로 실행
- service channel은 registry discovery 기반으로 연결
- actor/session relay는 application route channel을 새로 만들지 않고 ActorGateway 경로 사용
- Play server는 `useRegistrySpotRemoteAddresses("tictactoe")`에 대응하는 Java API 사용
- Session server는 relay ingress용 local SpotNode를 만들고 stream node는 그 SpotNode에
  `attachActorGateway(...)`
- session handler는 actor remote address resolver를 직접 호출하지 않음
- actor id/type과 ActorGateway locator로 `context.actors().bindAsync(...)`
- client reconnect 후 같은 actor id가 새 Session server에 다시 bind
- request/reply sequence가 session relay와 actor reply에 맞게 correlation
- Play actor는 `boundSession().send(...).submitAsync()`로 client push

금지 사항:

- sample-only route store
- sample-only metadata store
- session relay JSON packet
- in-memory route channel replacement
- readiness 문제를 숨기는 sleep 기반 우회

## 6. Bingo

Bingo sample은 `.NET` Bingo와 같은 matching room sample이다.

필수 흐름은 아래와 같다.

1. Registry, Api, Play, Session server를 별도 process로 실행한다.
2. client executable은 connector client 4개를 만든다.
3. 각 client는 Session stream endpoint에 연결하고 인증한다.
4. Session server는 actor를 준비하고 현재 stream session에 bind한다.
5. matching 요청은 bound actor로 relay된다.
6. Api server는 Play server에 room 배정을 요청한다.
7. Play server는 `BingoRoomSpot`을 만들거나 찾고 Entry Spot에서 room으로 join한다.
8. 첫 join actor를 host로 지정한다.
9. early start와 non-host start는 거절한다.
10. host start가 성공하면 room timer가 번호를 뽑는다.
11. room은 자동 mark와 승리 판정을 수행한다.
12. 같은 draw sequence의 복수 winner를 확인한다.
13. 모든 bound client session에 push notification이 도착한다.

## 7. StreamingClient

StreamingClient sample은 connector 자체의 사용성을 보여 준다.

- TCP와 WebSocket 중 하나 이상으로 server에 연결
- `MANUAL` dispatch mode에서 game loop처럼 `dispatchAsync()` 호출
- `send(...)`, `request(...)`, `on(...)` 사용
- packet name override와 metadata 사용
- request timeout과 error event 처리
- Kotlin coroutine client 예시

## 8. 검증 기준

Sample 구현 후 아래 command가 통과해야 한다.

```bash
./framework/languages/java/samples/run_samples.sh
```

이 command는 [implementation-execution-plan](./implementation-execution-plan.ko.md)
Phase 10 gate의 sample 실행 entry point다. 이 gate는 이전 phase의 unit,
contract, fake backend, integration gate를 대체하지 않는다. 특히 `.NET` 동등성의
기능 축은 [regression-test-matrix](./internals/regression-test-matrix.ko.md) 전체가
통과해야 닫힌다.

sample gate는 최소한 아래를 자동 확인한다.

- sample이 framework/connector public API만 사용한다.
- session sample에 route/metadata store가 없다.
- Session server가 ActorGateway attach를 사용한다.
- session handler가 actor remote address resolver를 직접 호출하지 않는다.
- connector client가 `MANUAL` dispatch mode에서도 request/reply와 notification을 처리한다.
- Bingo deterministic scenario가 같은 sequence winner를 만든다.
- TicTacToe SessionGateway reconnect scenario가 같은 actor id로 새 session binding을 만든다.
- [regression-test-matrix](./internals/regression-test-matrix.ko.md) §6의 모든 sample
  release gate 행을 미러한다.

최종 release 판정에서는 sample gate와 별도로 언어 간 상호호출 시나리오도
통과해야 한다. 이 항목은 sample 목록을 늘리기 위한 조건이 아니라, Java 서비스가
`.NET`/C++/Node 서비스와 같은 메시지 형식과 호출 규칙으로 상호 호출되는지
확인하기 위한 기능 축 검증이다.
