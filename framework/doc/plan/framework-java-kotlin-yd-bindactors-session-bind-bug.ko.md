# Java/Kotlin YieldDispatch BindActors session bind bug

이 문서는 Kotlin-only 우회를 막기 위한 재현 기록이다. Kotlin `YD-D1` local topology 검증을 강화하는
과정에서 공통 actor setup인 `BindActorsReq`가 Java/Kotlin 양쪽 YieldDispatch runner에서 timeout되는
현상이 확인되었다. 현재 checkout에서는 Java framework auto-connect 변경이 반영된 뒤 Kotlin focused
runner가 통과했으므로, 아래 실패 로그는 회귀 방지용 기록으로 유지한다.

## 재현 절차

Kotlin 재현:

```bash
cd framework/languages/java/e2e-kotlin/YieldDispatch
nice -n 10 timeout 240s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-B1
```

Java 재현:

```bash
cd framework/languages/java/e2e/YieldDispatch
nice -n 10 timeout 300s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-B1
```

두 runner 모두 자신이 시작한 process와 Redis container만 정리한다. 외부 process를 pattern으로 찾아
종료하지 않는다.

## 관찰 로그

- Kotlin: `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-232341-3929581`
  - `client.stderr.log`: `BindActorsReq`가 `request timed out after PT30S`로 실패한다.
  - `session-flow.log`: stream session이 `BindActorsReq`를 받고 route mesh로 보낸 뒤
    `REPLY_RECEIVED`까지 기록한다.
  - `play-a-flow.log`: play-a가 같은 `BindActorsReq`를 `RECEIVED`하고 `REPLIED`까지 기록한다.
- Kotlin current native check:
  `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-232830-3944234`
  - 같은 runner를 현재 `core/build/lib/libzlink.so`로 실행해도 `BindActorsReq`가
    `request timed out after PT30S`로 실패한다.
- Kotlin stream trace:
  `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-233036-3955917`
  - `session.stdout.log`: session handler가 actor bind를 시작한 뒤
    `session-actor bind-native-error ... TimeoutException:session relay route was not ready before timeout:
    actor-room-a`를 기록한다.
  - `session-flow.log`와 `play-a-flow.log`는 같은 실행에서도 route mesh `BindActorsReq`가
    session에서 `REPLY_RECEIVED`, play-a에서 `REPLIED`까지 진행했음을 보여 준다.
- Java: `framework/languages/java/e2e/YieldDispatch/logs/20260707-232452-3934593`
  - `client.stderr.log`: `BindActorsReq`가 `request timed out after PT30S`로 실패한다.
  - `session-flow.log`: `EnsureSpotReq` reply는 먼저 정상 수신되고, 이후 `BindActorsReq`도
    route mesh `REPLY_RECEIVED`까지 기록한다.
  - `play-a-flow.log`: play-a가 `BindActorsReq`를 `RECEIVED`하고 `REPLIED`까지 기록한다.

## 현재 checkout 검증

- Kotlin `YD-B1`: `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-233558-3987976`
  - `client.stdout.log`: `scenario YD-B1 passed`와 `yield-dispatch kotlin e2e result=passed`를 기록한다.
  - stream trace에서 `BindActorsReq` reply frame을 받은 뒤 actor join/yield request가 계속 진행된다.
- Kotlin `YD-D1`: `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-233650-3991306`
  - `client.stdout.log`: A/B/C/E1 local topology marker와 `scenario YD-D1 passed`,
    `yield-dispatch kotlin e2e result=passed`를 기록한다.
- Java `YD-B1`: `framework/languages/java/e2e/YieldDispatch/logs/20260707-233920-4006170`
  - `client.stdout.log`: `scenario YD-B1 passed`와 `yield-dispatch e2e result=passed`를 기록한다.

## 현재 판단

Kotlin 전용 scenario 코드나 D1 aggregate marker만의 문제가 아니다. Java non-Kotlin YieldDispatch에서도
같은 public stream connector -> session gateway -> route mesh -> play-a 경로가 같은 지점에서 timeout된다.
session handler가 play-a reply를 받은 뒤 actor-bound session bind를 시작하지만, Java framework의
session relay route readiness가 `play-a`를 준비 상태로 보지 못해 native bind 호출 전 단계에서
멈춘다. route mesh request/reply와 actor-bound session relay는 서로 다른 경로를 쓰므로, 이 현상은
Kotlin wrapper가 아니라 Java framework의 Spot mesh auto-connect/readiness 연동 문제로 좁힌다.

Kotlin E2E만 통과시키기 위해 actor bind를 건너뛰거나, raw frame을 직접 보내거나, timeout을 늘리는
우회는 넣지 않는다. `core/`는 수정하지 않는다. core/native 문제가 의심되면 C++/Node 또는 binding
수준 재현을 추가로 확인한 뒤 별도 core/framework bug fix 작업으로 처리한다.

## 후속 확인

관련 Java framework 변경을 정리할 때 이 문서의 실패 로그를 회귀테스트 입력으로 사용한다.
