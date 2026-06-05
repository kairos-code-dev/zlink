# [Java Binding Bug] `attachStreamLen32be` basic echo callback aborts with double free

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.2`
- Scope: `bindings/java`
- Affected area: `Socket.attachStreamLen32be(...)` callback ownership/lifecycle

## Summary

Java binding의 `STREAM + attachStreamLen32be` callback 경로가 basic echo 수준의
최소 재현에서도 프로세스를 abort 시킵니다.

Observed stderr:

```text
double free or corruption (out)
```

이 문제는 perf 코드 특유의 복잡한 흐름이 아니라,
`attachStreamLen32be` 하나만 사용한 최소 echo에서도 재현됩니다.

## Minimal Reproduction

서버:

- `SocketType.STREAM`
- `bind(endpoint)`
- `attachStreamLen32be((rid, messages) -> { for (Message m : messages) server.streamSend(rid, m, SendFlag.NONE); return 0; })`

클라이언트:

- raw TCP socket으로 1개 LEN32BE frame 전송
- echo frame 읽기

실행 결과:

```text
double free or corruption (out)
```

프로세스는 정상 종료하지 못하고 abort 됩니다.

## Why This Looks Binding-Side

같은 native runtime(`core/v4.0.2`)에서:

- .NET binding은 `stream_callback_echo_len32be`,
  `stream_callback_len32be_transfers_message_ownership` 테스트를 갖고 있음
- C++ `MULTI_STREAM_LEN32BE tcp/64`도 통과

즉, core STREAM LEN32BE 자체가 전역적으로 깨졌다기보다
Java callback ownership 처리 쪽이 더 의심됩니다.

## Current Java Signals

1. `MULTI_STREAM_LEN32BE` perf server가 `v4.0.2`에서도 동일하게 abort
2. basic standalone repro도 동일 계열로 abort
3. `attachStreamRaw` 경로와 달리 `attachStreamLen32be`만 문제

## v4.0.2 Re-Check

Direct repro with Java server + shared core stream client:

```bash
java --enable-native-access=ALL-UNNAMED \
  -cp bindings/java/perf/multi/Zlink.PerfBench/build/classes/java/main:bindings/java/build/classes/java/main:bindings/java/build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server MULTI_STREAM_LEN32BE tcp 64
```

Client:

```bash
core/perf/common/streamclient/build/perf_stream_shared_client \
  --pattern MULTI_STREAM_LEN32BE --transport tcp --endpoint <READY> \
  --sizes 64 --runs 1 --ccu 1 --warmup 0 --duration 1 \
  --lat-count 200 --io-threads 1 --latency-sample-rate 1 \
  --print-perf-result 2 --send-stop-token 1 \
  --stop-token __zlink_perf_stop__
```

Observed server log:

```text
READY,tcp://127.0.0.1:14431
double free or corruption (out)
```

Observed exit:

```text
SERVER_RC=134
```

## Candidate Binding Problem Areas

우선순위 높은 의심 구간:

1. `Socket.onStreamPackets(...)`
   - [Socket.java](../../bindings/java/src/main/java/systems/zlink/contracts/sockets/Socket.java#L720)
2. `Message.fromMsgVector(...)`
   - [Message.java](../../bindings/java/src/main/java/systems/zlink/contracts/messaging/Message.java#L258)
3. `streamSend(..., Message, ...)` ownership consumption contract
   - [Socket.java](../../bindings/java/src/main/java/systems/zlink/contracts/sockets/Socket.java#L506)

특히 Java는 .NET과 달리 `LEN32BE` callback regression test가 없었고,
이번 조사 중 `streamSend(..., Message, ...)` ownership 처리도 .NET과 차이가
있어 일부 정렬했지만, crash는 여전히 남아 있습니다.

## Expected

- callback이 받은 `Message[]`를 `streamSend(rid, message, SendFlag.NONE)`로
  consume 가능해야 함
- raw TCP client가 동일 payload를 echo로 받아야 함
- abort / heap corruption / double free 없어야 함

## Actual

- 첫 frame echo 시도에서 process abort
- stderr: `double free or corruption (out)`

## Suggested Next Steps

1. Java integration test 추가
   - dotnet의 `stream_callback_echo_len32be`
   - dotnet의 `stream_callback_len32be_transfers_message_ownership`
2. Java `onStreamPackets`에서 native msg vector cleanup 방식과
   .NET `CloseStreamPacketRange` 경로를 1:1 비교
3. `Message.fromMsgVector(...)` 이후 `Message` ownership이
   `streamSendMsg`와 충돌하지 않는지 확인
4. 필요하면 `LEN32BE` callback path만 별도 native shim으로 분리

## Non-Workaround Note

- perf 구현에서 plain STREAM으로 우회하지 않음
- retry/sleep/fallback으로 성공처럼 보이게 만들지 않음
- 현 상태는 Java binding blocker로 유지
