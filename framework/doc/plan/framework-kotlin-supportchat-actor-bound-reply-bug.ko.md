# Kotlin SupportChat actor-bound reply bug

이 문서는 구현 완료 보고가 아니라 재현 기록이다. Kotlin `SupportChat` sample runner에서 재접속한
agent가 `SetAgentAvailableReq(false)`를 보내면 support actor handler는 reply하지만 stream client가 reply
frame을 받지 못한다.

## 재현 절차

```bash
cd framework/languages/java/samples/kotlin/SupportChat
SUPPORTCHAT_KEEP_RUN_DIR=1 nice -n 10 timeout 300s ./run_sample.sh
ZLINK_JAVA_STREAM_TRACE=1 SUPPORTCHAT_KEEP_RUN_DIR=1 nice -n 10 timeout 300s ./run_sample.sh
```

runner는 자신이 시작한 Redis container와 child process만 cleanup한다. 외부 process를 pattern으로
종료하지 않는다.

## 관찰 로그

- `/tmp/tmp.OVoz83uF8G/logs/client.log`
  - `supportchat-closed-typing-ignore=verified` 출력 후 `TimeoutException`이 발생한다.
- `/tmp/tmp.OVoz83uF8G/sample-logs/flow-session.log`
  - `SetAgentAvailableReq corr=5`가 stream session에 `RECEIVED`로 기록되지만 `REPLIED`가 없다.
- `/tmp/tmp.OVoz83uF8G/sample-logs/flow-support.log`
  - 같은 `SetAgentAvailableReq corr=5 actor=agent-1`이 support actor handler에서 `RECEIVED`와
    `REPLIED`로 기록된다.
- `/tmp/tmp.867Qdp2xxR/logs/client.log`
  - stream trace 실행에서도 corr=5 request write 뒤 reply frame 없이 timeout된다.
- `/tmp/tmp.6zOPg638eh/logs/support.log`
  - 재접속 뒤 `agent-1`과 conversation actor들이 `sourceSession=6`으로 다시 bind된다.
  - `SetAgentAvailableReq corr=5` 처리 시 support 쪽 `sendActorBoundSession` 호출은 성공으로 반환되지만
    client reply frame은 없다.
- `/tmp/tmp.txphDtjFaH/logs/client.log`
  - actor request reply를 source session으로 직접 돌려보내는 실험은 초기 `SetAgentAvailableReq`부터
    timeout되어 core/binding reply 계약과 맞지 않았다. 이 우회는 남기지 않는다.

## 비교 범위

- Java `SupportChat` 기본 runner는 2026-07-07 trace 실행에서
  `supportchat full client/server self-check completed`로 통과했다.
- 다만 Java runner는 재접속 뒤 `SetAgentAvailableReq(false)` no-agent 경로를 검증하지 않으므로 이번
  blocker를 닫는 증거가 아니다.
- 요청은 session에서 support actor까지 도달하고 support handler도 reply하므로, Kotlin client DSL이나
  SupportChat domain code만의 문제로 단정하지 않는다.

## 현재 판단

Kotlin sample만 통과시키기 위해 raw frame, sleep 기반 retry, source session 직접 reply 같은 우회를 넣지
않는다. 증상은 재접속된 stream session으로 actor-bound reply가 돌아오지 않는 경로 문제다. Java 공용
framework, Java binding, root core stream actor binding 중 어느 계층에서 route가 stale 상태로 남는지
별도 확인이 필요하다.

현재 작업은 `core/`를 수정하지 않으므로, 이 문제는 Kotlin sample 완료 항목으로 닫지 않는다.

## 후속 확인

1. Java `SupportChat` runner에 같은 재접속 no-agent path를 추가해 Java 공용 framework에서도 재현되는지
   확인한다.
2. Node/C++ `SupportChat` 또는 actor-bound session reply scenario에서 같은 흐름이 통과하는지 확인한다.
3. Java/Kotlin 공통 실패면 Java framework runtime/session actor binding 계층에서 먼저 수정하고,
   binding 또는 core 공통 실패가 확인될 때만 별도 core bug fix 작업으로 넘긴다.
