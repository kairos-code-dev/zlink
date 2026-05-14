# Java multi STREAM public client API gap

## 요약

`bindings/java/perf/multi/run_benchmarks.sh`의 `MULTI_STREAM` client 경로는
아직 Java binding의 public API만으로 실행되지 않는다. 현재 runner는 C perf
바이너리인 `bindings/c/build/perf/perf_stream_client`를 client로 호출한다.

이는 `doc/perf/PERF_POLICY.md`의 다음 기준과 맞지 않는다.

- binding perf는 해당 언어 binding의 public API로 data path를 직접 실행해야 한다.
- C 기준 perf 바이너리나 다른 언어 perf 바이너리를 wrapper로 호출한 결과는
  비교 대상으로 인정하지 않는다.
- 공개 API 동작에 문제가 있으면 perf 코드에서 우회하지 않고 버그로 보고한다.

## 현재 상태

Java `StreamSocket` public API는 server-oriented surface인 `bind`,
`onPacket`, `send(RoutingId, ...)` 중심으로 구성되어 있다. Java public API에는
STREAM client가 서버에 접속해서 packet handler surface로 송수신할 수 있는
공개 `connect` 경로가 없다.

기존 Java 테스트도 `StreamSocket`에 public `connect` 또는 내부 stream helper가
노출되지 않는다는 계약을 확인한다. 따라서 Java perf에서 C wrapper를 제거하려면
단순 perf 코드 수정이 아니라 Java binding public API 계약 결정이 먼저 필요하다.

## 필요한 후속 작업

1. Java binding에 STREAM client public API를 추가할지 결정한다.
2. API를 추가한다면 spec/draft로 계약을 먼저 정리하고, Java binding 회귀 테스트를
   추가한다.
3. `bindings/java/perf/multi`의 `MULTI_STREAM` client를 Java public API 기반으로
   교체한다.
4. API를 추가하지 않는다면 Java perf 기본 matrix에서 `MULTI_STREAM`을 제외하고,
   C perf와 비교할 때 Java STREAM은 unsupported로 명확히 출력해야 한다.

현재 기본 matrix에는 `MULTI_STREAM`이 포함되어 있으므로, 단순히 숨기거나 기존 C
wrapper 경로를 유지하는 것은 최종 정렬 상태로 볼 수 없다.
