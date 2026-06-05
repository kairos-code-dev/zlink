# [Core Bug Report] MULTI_SPOT pollable mode에서 Java client native heap corruption

- Date: 2026-03-06
- Repo commit: `16baa6c7546d0c737275395026242cfabc0c4480`
- Reporter area: `bindings/java/perf/multi` (Spot pollable mode 적용 후 검증)
- Severity: High (process abort / benchmark 불가)

## 1) 요약

`MULTI_SPOT`를 **Spot facade가 아닌 pollable transport mode**로 실행하면, Java client 프로세스가 native allocator corruption으로 비정상 종료됩니다.

- Client exit code: `-6` (SIGABRT)
- stderr:
  - `corrupted double-linked list`
  - `malloc(): unsorted double linked list corrupted`

동일 구간에서 server는 정상 종료(`rc=0`)하며 `READY`와 server metrics를 출력합니다.

정책상 이 경로는 우회 불가입니다.
- Spot facade mode와 pollable mode 혼용 금지(EFSM)
- multi perf Spot은 pollable socket 기반 event-loop 경로를 사용해야 함

## 2) 정책/계약 배경

Core/Binding mode split 계약:
- `SpotNode.pubSocket()/subSocket()` 호출 시 pollable mode 진입
- 같은 node에서 `Spot.publish()/Spot.recv()` facade I/O 혼용 시 EFSM

관련 코드/테스트:
- core mode split check: [core/src/services/spot/spot_node.cpp](../../core/src/services/spot/spot_node.cpp:237)
- core test (EFSM): [core/tests/spot/test_spot_mode_split.cpp](../../core/tests/spot/test_spot_mode_split.cpp:31)
- java integration test (EFSM): [bindings/java/src/test/java/dev/kairoscode/zlink/integration/TestServiceModeSplitPortedTest.java](../../bindings/java/src/test/java/dev/kairoscode/zlink/integration/TestServiceModeSplitPortedTest.java:39)

## 3) 재현 환경

- OS: Ubuntu 24.04 (WSL2)
- JDK: Temurin 22.0.2
- libzlink: `3.0.1`
- Java native path:
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/java/src/main/resources/native/linux-x86_64/libzlink.so`
  - `LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/java/src/main/resources/native/linux-x86_64`

## 4) 재현 절차

### A. 정책 런너 경유 재현

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding java \
  --suite multi \
  --pattern MULTI_SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --multi-duration-seconds 1 \
  --multi-clients 10 \
  --reuse-build \
  --result
```

관측:
- 실패 사유: `non_zero_exit_2`
- 결과 파일: `/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/tmp/perf_linux_20260306_191417.txt`

### B. split server/client 직접 재현 (런너와 동일 env)

Server command:
```bash
/usr/bin/java --enable-native-access=ALL-UNNAMED \
  -cp /home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.PerfBench/build/classes/java/main:/home/hep7/project/kairos/zlink/bindings/java/build/classes/java/main:/home/hep7/project/kairos/zlink/bindings/java/build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server MULTI_SPOT tcp 64
```

Client command:
```bash
/usr/bin/java --enable-native-access=ALL-UNNAMED \
  -cp /home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.PerfBench/build/classes/java/main:/home/hep7/project/kairos/zlink/bindings/java/build/classes/java/main:/home/hep7/project/kairos/zlink/bindings/java/build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client MULTI_SPOT tcp 64 --endpoint <READY endpoint>
```

#### clients=1
- `CLIENT_RC=-6`
- client stderr: `corrupted double-linked list`
- `SERVER_RC=0`
- server stdout:
  - `READY,tcp://127.0.0.1:27425`
  - `RESULT,current,MULTI_SPOT,tcp,64,server_cpu_pct,...`
  - `RESULT,current,MULTI_SPOT,tcp,64,server_mem_mb,...`

#### clients=10
- `CLIENT_RC=-6`
- client stderr: `malloc(): unsorted double linked list corrupted`
- `SERVER_RC=0`
- server stdout:
  - `READY,tcp://127.0.0.1:4683`
  - `RESULT,current,MULTI_SPOT,tcp,64,server_cpu_pct,...`
  - `RESULT,current,MULTI_SPOT,tcp,64,server_mem_mb,...`

## 5) 기대 동작 vs 실제 동작

기대:
- pollable Spot SUB recv 경로에서 client가 정상 종료
- throughput/latency RESULT 출력

실제:
- client가 native heap corruption으로 abort
- metrics 미출력, 조합 실패

## 6) 적용된 Java 경로 (혼용 없음)

아래 구현은 facade I/O(`Spot.publish/recv`)를 사용하지 않고, pollable socket 경로만 사용합니다.

Server:
- `pubNode.pubSocket()` 사용: [PerfMultiSpotServer.java](../../bindings/java/perf/multi/Zlink.PerfBench/src/main/java/dev/kairoscode/zlink/integration/bench/src/PerfMultiSpotServer.java:90)
- `DONTWAIT_SNDMORE` + `DONTWAIT` send: [PerfMultiSpotServer.java](../../bindings/java/perf/multi/Zlink.PerfBench/src/main/java/dev/kairoscode/zlink/integration/bench/src/PerfMultiSpotServer.java:189)

Client:
- `node.subSocket()` + raw `SUBSCRIBE`: [PerfMultiSpotClient.java](../../bindings/java/perf/multi/Zlink.PerfBench/src/main/java/dev/kairoscode/zlink/integration/bench/src/PerfMultiSpotClient.java:100)
- `Poller` + `recv(DONTWAIT)` drain: [PerfMultiSpotClient.java](../../bindings/java/perf/multi/Zlink.PerfBench/src/main/java/dev/kairoscode/zlink/integration/bench/src/PerfMultiSpotClient.java:117)

즉, "facade/pollable 혼용으로 인한 EFSM"은 해당 재현에서 원인이 아닙니다.

## 7) Core 의심 구간 (가설)

확정 원인은 core 분석이 필요하지만, mode split 이후에도 control thread가 `_sub`를 조작하는 경로가 남아 있어 동시성 충돌 가능성이 있습니다.

관련 코드:
- pollable mode 플래그 set: [core/src/services/spot/spot_node.cpp](../../core/src/services/spot/spot_node.cpp:935)
- `process_sub()`는 pollable mode에서 skip: [core/src/services/spot/spot_node.cpp](../../core/src/services/spot/spot_node.cpp:1612)
- 하지만 `flush_pending()`은 `_sub->setsockopt/connect/term_endpoint` 수행: [core/src/services/spot/spot_node.cpp](../../core/src/services/spot/spot_node.cpp:1562)
- control thread tick에서 `flush_pending()` 호출: [core/src/services/spot/spot_node.cpp](../../core/src/services/spot/spot_node.cpp:1784)

참고: 위는 코드 기반 가설이며, 실제 corruption root-cause는 native stack/asan/valgrind 확인이 필요합니다.

## 8) 교차 신호 (참고)

- Java `MULTI_GATEWAY tcp/64`는 정상 통과 (Poller 일반 경로는 동작)
  - 결과: `/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/tmp/perf_linux_20260306_192232.txt`
- Java `single SPOT tcp/64`는 facade mode로 정상 통과
  - 결과: `/home/hep7/project/kairos/zlink/bindings/java/perf/results/single/tmp/perf_linux_20260306_192241.txt`
- Dotnet/C++ `MULTI_SPOT`도 현재 실패/timeout 관측
  - dotnet: `/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/tmp/perf_linux_20260306_192107.txt`
  - cpp: `/home/hep7/project/kairos/zlink/bindings/cpp/perf/results/multi/tmp/perf_linux_20260306_192128.txt`

## 9) 요청 사항

1. `SPOT pollable SUB recv` 경로의 native heap corruption 원인 분석
2. 필요 시 pollable mode에서 control-thread의 `_sub` 조작 범위 제한/동기화 보강
3. core regression test 추가:
   - `spot_node_sub_socket()` + raw SUBSCRIBE + poll/recv(dontwait) sustained loop
   - multi-client(1, 10+) with warmup/active phase
4. 재현 불가 시, mode split 계약에서 허용/금지 호출 시점(특히 `connectPeerPub`, `subSocket`, raw SUBSCRIBE 순서)의 명시 강화

## 10) 비우회 원칙 확인

이번 리포트의 구현/검증은 아래를 지켰습니다.
- retry budget/cap/fallback으로 실패 숨기기 미사용
- 실패는 그대로 non-zero exit와 stderr로 보고
- 정책 위배 우회 없이 pollable mode 경로로 재현
