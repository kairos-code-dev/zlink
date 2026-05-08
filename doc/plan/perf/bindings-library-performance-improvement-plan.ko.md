# bindings 라이브러리 성능 개선 실행 계획

> 작성일: 2026-05-08
>
> 목적: `bindings/c/perf` 결과를 기준선으로 삼아 언어별 binding 라이브러리의
> 성능을 목표 비율까지 끌어올리는 작업 순서와 반복 절차를 고정한다.
>
> 기준 정책:
> - [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
> - [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
> - [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

## TODO

- [ ] 기준 C perf 재측정 및 기준 파일 고정
- [ ] C++ 목표 달성
- [ ] .NET 목표 달성
- [ ] Java 목표 달성
- [ ] Node 목표 달성
- [ ] Python 목표 달성
- [ ] Go 목표 달성
- [ ] Rust 목표 달성
- [ ] 전체 언어 최종 재측정 및 결과 요약

## 1. 범위와 목표

이번 작업은 perf 자체를 빠르게 만드는 일이 아니라, 각 언어 binding 라이브러리가
public API를 통해 내는 실제 성능을 개선하는 일이다. perf는
`doc/perf` 정책에 맞게 이미 작성되어 있다고 보고, perf 자체의 측정 버그가
확인된 경우를 제외하면 수정하지 않는다.

목표 비율은 같은 suite, pattern, transport, message size, metric 조합에서
`bindings/c/perf` 결과를 기준으로 계산한다.

| 순서 | 언어 | perf 경로 | 목표 |
|------|------|-----------|------|
| 1 | C++ | `bindings/cpp/perf` | C 기준 90% 이상 |
| 2 | .NET | `bindings/dotnet/perf` | C 기준 85% 이상 |
| 3 | Java | `bindings/java/perf` | C 기준 85% 이상 |
| 4 | Node | `bindings/node/perf` | C 기준 70% 이상 |
| 5 | Python | `bindings/python/perf` | C 기준 70% 이상 |
| 6 | Go | `bindings/go/perf` | C 기준 80% 이상 |
| 7 | Rust | `bindings/rust/perf` | C 기준 90% 이상 |

비교의 1차 기준은 `throughput`이다. `bandwidth`는 같은 payload 크기에서
throughput과 같은 방향으로 움직여야 하며, `latency`, `latency_p95`,
`latency_p99`가 크게 악화되면 목표 비율을 만족해도 완료로 보지 않는다.
레이턴시는 낮을수록 좋기 때문에, C 기준보다 느린 정도가 해당 언어 목표 비율의
역수 안에 들어오는지 함께 본다. 예를 들어 90% 목표 언어는 레이턴시가 C의
약 `1 / 0.90` 배를 크게 넘지 않아야 한다.

## 2. 고정 전제

- `bindings/c/perf`가 기준선이다. 기준선이 낡으면 먼저 C perf를 다시 실행해
  같은 날짜의 기준 파일을 고정한다.
- `bindings/c/perf`를 실행하기 전에 `core/build` runtime을 최신으로 만든다.
  `core/src/` 또는 `core/include/`를 바꾼 뒤에는 반드시
  `cmake --build core/build`를 먼저 실행한다.
- `bindings/c/perf` 수치는 `core/build`의 `libzlink.so` 기준으로만 해석한다.
  `build_cpp_release`나 임시 빌드 디렉터리의 runtime으로 C 기준을 만들지 않는다.
- 각 언어는 위 표의 순서대로 하나씩 끝낸다. 앞 언어가 목표를 달성하기 전에는
  다음 언어 최적화로 넘어가지 않는다.
- 각 언어 perf는 해당 언어 binding의 public API만 사용해야 한다. native core
  perf 바이너리를 호출해 결과만 중계하는 방식은 측정으로 인정하지 않는다.
- perf 측정 의미, RESULT line, ready/active phase, fail/skip/unsupported 의미는
  `doc/perf` 정책과 동일하게 유지한다.
- retry, inflight 제한, sleep 기반 보정, 숨은 fallback으로 수치를 만들지 않는다.
- 이 작업은 사람의 추가 판단을 기다리지 않고 진행한다. 모든 언어가 목표를
  달성할 때까지 자동으로 다음 측정, 분석, 수정, 검증 단계로 넘어간다.

## 3. 무중단 자동 운영 원칙

이 계획은 사람이 중간에 방향을 다시 정해 주지 않아도 끝까지 진행할 수 있어야
한다. 작업자는 아래 원칙에 따라 스스로 다음 행동을 결정한다.

### 3.1 중단하지 않는 기본 루프

전체 작업은 아래 루프를 목표 달성까지 반복한다.

1. 현재 대상 언어를 고른다.
2. C 기준과 대상 언어 기준을 측정한다.
3. 목표 미달 조합을 찾는다.
4. 가장 큰 병목 하나를 고른다.
5. perf가 아니라 라이브러리 또는 core를 수정한다.
6. 필요한 회귀 테스트를 추가하거나 갱신한다.
7. 테스트와 perf smoke를 실행한다.
8. 목표 조합을 다시 측정한다.
9. 목표 조합이 기준을 만족하면 full perf로 언어 완료 여부를 확인한다.
10. 해당 언어를 완료 처리하고 다음 언어로 넘어간다.

이 루프는 실패를 만나도 멈추지 않는다. 실패는 다음 작업 항목으로 변환한다.

| 실패 유형 | 자동 다음 행동 |
|-----------|----------------|
| build 실패 | 실패 로그를 읽고 가장 가까운 컴파일/링크 오류부터 수정한다 |
| 테스트 실패 | 실패 테스트를 기준으로 원인을 좁히고 라이브러리 구현을 고친다. 테스트 기대값은 공개 헤더와 정책이 틀렸다고 확인된 경우에만 수정한다 |
| perf partial | 실패 조합을 bug 항목으로 기록하고 fail 원인을 먼저 고친다 |
| 목표 비율 미달 | 가장 손실이 큰 조합을 다음 병목 분석 대상으로 고른다 |
| 결과 흔들림 | 성공한 같은 조건을 반복 측정하고 median 기준으로 판단한다. 실패한 조합을 통과시키기 위한 재시도는 하지 않는다 |
| 환경 문제 | ulimit, 포트 충돌, stale build, runtime 경로를 자동으로 점검하고 고친다 |

### 3.2 사람에게 묻지 않는 의사결정 규칙

아래 선택은 사람에게 묻지 않고 결정한다.

- 어떤 언어를 다음에 할지: 이 문서의 순서를 따른다.
- 어떤 조합을 먼저 볼지: 목표 대비 ratio가 가장 낮은 조합을 먼저 본다.
- 어떤 metric을 우선할지: `complete` 여부, 64B throughput, 큰 메시지 bandwidth,
  latency triplet 순서로 본다.
- perf를 고칠지 말지: 정책 위반 또는 측정 버그일 때만 고친다.
- core와 binding 중 어디를 고칠지: 여러 언어에서 같은 증상이면 core를 먼저 보고,
  특정 언어에서만 보이면 해당 binding을 먼저 본다.
- 테스트를 어디에 둘지: core 버그는 core 테스트, binding 버그는 해당 binding
  테스트에 둔다.
- 변경이 실패하면 어떻게 할지: 실패한 변경을 작게 되돌리거나 더 작은 가설로
  나눈 뒤 즉시 다음 라운드를 실행한다.

### 3.3 자동 확장 조건

사람의 판단을 기다리는 대신, 아래 조건을 만나면 작업 범위를 자동으로 확장한다.
이 경우에도 해당 언어를 끝내기 전에는 다음 언어로 넘어가지 않는다.

- 공개 API 계약 변경이 필요하면 `doc/spec/draft/`에 구현 전 초안을 먼저 작성한 뒤,
  `core/include/zlink.h`, 테스트, errno 문서, binding 문서를 맞추는 작업까지
  이어 간다.
- core 병목이면 core 개선 작업으로 전환하고, core C perf 기준선을 다시 만든 뒤
  대상 언어 측정을 계속한다.
- 특정 언어 runtime 제한이 병목이면 public API 의미를 유지하는 범위에서 buffer
  재사용, native handle 소유권, 호출 횟수 축소 같은 대안을 계속 시도한다.
- 같은 가설이 반복해서 실패하면 실패한 가설 목록에 남기고 다음 가설로 넘어간다.

자동 확장 라운드는 반드시 아래 정보를 남긴다.

- 마지막 C 기준 파일과 대상 언어 결과 파일.
- 목표 미달 조합 목록.
- 이미 시도한 변경과 배제한 가설.
- 병목이 public API, runtime, core, binding 중 어디에 있는지에 대한 근거.
- 다음 자동 작업 항목과 검증 명령.

### 3.4 전체 종료 조건

전체 작업은 아래 조건이 충족될 때만 끝난다.

- 모든 언어가 목표 비율을 만족하고 full single + full multi가 complete로 끝났다.

그 외 상황에서는 작업을 멈추지 않는다. 실행 환경 장애도 완료 조건이 아니며,
복구 명령, 빌드 재생성, 포트 정리, ulimit 조정 같은 자동 복구 작업으로 변환한다.
분석만 하고 중단하지 않으며, 항상 다음 측정 또는 다음 수정으로 이어 간다.

## 4. 기준선 수립

각 작업 라운드의 첫 단계는 같은 환경에서 C 기준과 대상 언어 현재 상태를
나란히 고정하는 것이다.

```bash
cmake --build core/build

bindings/c/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64 --results-tag baseline_YYYYMMDD
bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64 --results-tag baseline_YYYYMMDD

bindings/<lang>/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64 --results-tag baseline_YYYYMMDD
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64 --results-tag baseline_YYYYMMDD
```

64B smoke가 complete이면 full matrix를 실행한다.

```bash
bindings/c/perf/run_benchmarks.sh --pattern ALL --results-tag full_YYYYMMDD
bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --results-tag full_YYYYMMDD

bindings/<lang>/perf/run_benchmarks.sh --pattern ALL --results-tag full_YYYYMMDD
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern ALL --results-tag full_YYYYMMDD
```

full matrix가 너무 오래 걸리면 먼저 목표 미달 조합을 좁혀서 작업하되, 완료
판정은 최종 full single + full multi 결과로 한다. smoke 수치는 기능 확인용이며
최종 목표 달성 근거로 쓰지 않는다.

## 5. 비교 방법

결과 비교는 `RESULT` line의 동일 조합끼리 한다. 동일 조합이란 아래 값이 모두
같은 행을 뜻한다.

- suite: single 또는 multi
- pattern
- transport
- message size
- metric

throughput과 bandwidth는 높을수록 좋다.

```text
throughput_ratio = target_lang_throughput / c_throughput
bandwidth_ratio  = target_lang_bandwidth  / c_bandwidth
```

latency 계열은 낮을수록 좋다.

```text
latency_ratio     = c_latency     / target_lang_latency
latency_p95_ratio = c_latency_p95 / target_lang_latency_p95
latency_p99_ratio = c_latency_p99 / target_lang_latency_p99
```

각 ratio가 언어별 목표 이상이면 해당 metric은 통과로 본다. 예를 들어 Java의
목표는 85%이므로 `0.85` 이상이어야 한다. 단, latency 계열은 측정 흔들림이
크기 때문에 단일 행만 보고 결론을 내리지 않는다. throughput 목표를 만족한 뒤에도
`latency_p95` 또는 `latency_p99`가 반복 실행에서 계속 나빠지면 별도 병목으로
다룬다.

결과 해석 우선순위는 아래와 같다.

1. `complete` 여부: partial이면 먼저 실패 조합을 고친다.
2. 64B throughput: binding boundary 비용이 가장 잘 드러나는 기준이다.
3. 큰 메시지 bandwidth: 복사와 buffer 재사용 문제가 잘 드러난다.
4. latency triplet: 이벤트 루프, poller, runtime scheduling 문제가 잘 드러난다.
5. pattern별 편차: 특정 패턴만 낮으면 공통 binding보다 해당 socket wrapper를 먼저 본다.

## 6. 반복 작업 루프

각 언어는 아래 루프를 반복한다.

1. C 기준 파일과 대상 언어 결과 파일을 고른다.
2. 같은 조합끼리 `throughput`, `bandwidth`, `latency`, `latency_p95`,
   `latency_p99`를 비교한다.
3. 목표 미달 조합을 `pattern / transport / size / metric` 단위로 정리한다.
4. 가장 큰 손실이 있는 hot path를 하나 고른다.
5. binding 라이브러리나 core 라이브러리에서 병목을 줄이는 최소 변경을 만든다.
6. 변경 범위에 맞는 회귀 테스트를 먼저 실행한다.
7. 대상 조합 smoke를 실행한다.
8. single + multi smoke를 실행한다.
9. 목표 조합을 다시 측정해 개선 여부를 확인한다. 언어 완료 판정 때는 full perf를
   실행해 목표 비율을 다시 계산한다.
10. 결과 파일, 변경 요약, 남은 미달 조합을 이 계획 문서나 별도 로그에 남긴다.

한 번에 여러 가설을 섞지 않는다. 같은 라운드에 변경을 많이 넣으면 어떤 변경이
성능을 올렸는지 판단할 수 없고, 회귀가 생겼을 때 되돌릴 경계도 흐려진다.

### 6.1 라운드 기록 양식

각 라운드가 끝나면 아래 형식으로 기록한다. 기록 위치는 이 문서의 하단 또는
언어별 별도 로그 문서로 둔다.

```markdown
### YYYY-MM-DD <lang> round N

- 기준 C 결과:
- 대상 언어 결과:
- 목표 미달 조합:
- 선택한 병목 가설:
- 변경한 라이브러리 파일:
- 추가/수정한 회귀 테스트:
- 실행한 검증 명령:
- 결과:
- 다음 판단:
```

목표 미달 조합은 가능한 한 구체적으로 적는다.

```text
suite=<single|multi>, pattern=<name>, transport=<name>, size=<bytes>,
metric=<throughput|bandwidth|latency|latency_p95|latency_p99>,
c=<value>, lang=<value>, ratio=<value>, target=<value>
```

## 7. perf 수정 금지 범위

이번 작업에서 perf 수정은 예외다. 아래 경우만 perf를 수정할 수 있다.

- RESULT line 파싱이나 출력이 정책과 다르게 구현된 경우.
- 같은 payload를 측정한다고 표시하지만 실제 metric header, phase, run_id,
  active 집계 조건이 C 기준과 다른 경우.
- runner가 stale runtime을 쓰거나, 공식 entrypoint 계약과 다르게 동작하는 경우.
- perf 코드가 해당 언어 binding public API가 아니라 내부 API나 native perf를
  직접 호출하는 경우.

아래 수정은 금지한다.

- timeout을 늘려 실패를 통과처럼 보이게 하는 수정.
- retry를 추가해 실패를 숨기는 수정.
- inflight/outstanding 제한으로 throughput을 인위적으로 안정화하는 수정.
- 측정 구간 밖에서 보내야 할 메시지를 active 집계에 포함하는 수정.
- 특정 언어에만 유리하도록 pattern, transport, size 기본값을 바꾸는 수정.

perf 버그가 아닌데 perf를 바꾸고 싶어지는 상황은 대부분 라이브러리 문제나
측정 환경 문제다. 이 경우 perf를 고치지 말고 라이브러리 또는 환경을 고친다.

## 8. 버그 처리 규칙

perf 실행 중 실패가 나오면 먼저 실패를 정상 신호로 취급한다.

| 상황 | 처리 |
|------|------|
| perf 자체 측정 버그 | perf를 수정할 수 있다. 단, RESULT 의미와 정책 계약을 바꾸지 않고 smoke로 검증한다 |
| binding 라이브러리 버그 | perf에서 우회하지 않는다. 해당 binding 회귀 테스트를 추가하고 라이브러리를 수정한다 |
| core 라이브러리 버그 | perf에서 우회하지 않는다. core 회귀 테스트를 추가하고 core를 수정한 뒤 perf를 다시 실행한다 |
| 환경 문제 | fd limit, 포트 충돌, stale runtime 같은 원인을 수정하고 같은 명령을 다시 실행한다 |
| 정책 미지원 조합 | 정책에 정의된 조합인지 확인한 뒤, 정의되지 않은 조합만 unsupported로 둔다 |

라이브러리 버그를 고칠 때는 아래 순서를 지킨다.

1. 실패 조합과 로그를 `doc/bug/perf/` 또는 관련 bug 문서에 기록한다.
2. 재현 가능한 회귀 테스트를 추가한다.
3. 회귀 테스트가 실패하는 것을 확인한다.
4. core 또는 binding 라이브러리를 수정한다.
5. 회귀 테스트가 통과하는 것을 확인한다.
6. perf smoke와 목표 조합 측정을 다시 실행한다.

## 9. 언어별 작업 기준

### 9.1 C++

- 목표: C 기준 90% 이상.
- 우선 확인 지점:
  - C++ wrapper가 불필요한 heap allocation이나 문자열 변환을 hot path에 넣는지 확인한다.
  - RAII wrapper가 소켓 ownership은 지키면서 send/recv 경로에 과한 동기화를
    추가하지 않는지 확인한다.
  - C API와 동일한 recv drain, poller, metric header 처리 의미를 유지한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.2 .NET

- 목표: C 기준 85% 이상.
- 우선 확인 지점:
  - P/Invoke boundary에서 per-message allocation, array pinning, marshal 비용이
    반복되는지 확인한다.
  - buffer 재사용, span 기반 API, handle ownership이 public API 의미를 해치지
    않는 범위에서 적용되는지 확인한다.
  - ReadyToRun, tiered compilation 옵션이 정책 권장값과 맞는지 확인한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.3 Java

- 목표: C 기준 85% 이상.
- 우선 확인 지점:
  - JNI boundary에서 byte array copy가 반복되는지 확인한다.
  - direct buffer, native handle lifecycle, exception 변환이 hot path에 들어가는지
    확인한다.
  - `-server`, `-XX:TieredStopAtLevel=1` 등 perf 실행 옵션이 정책 권장값과 맞는지
    확인한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.4 Node

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - N-API boundary에서 Buffer copy와 JS object 생성이 per-message로 반복되는지
    확인한다.
  - event loop 연동이 recv drain을 충분히 진행하는지 확인한다.
  - TSFN 같은 direct callback 동기화 비용을 perf 기본 surface에 끌어들이지
    않았는지 확인한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.5 Python

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - C extension boundary에서 bytes 생성과 GIL 잡는 구간이 hot path를 막는지
    확인한다.
  - reusable buffer나 memoryview를 public API 의미 안에서 쓸 수 있는지 확인한다.
  - recv drain을 Python 루프가 불필요하게 잘게 쪼개지 않는지 확인한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.6 Go

- 목표: C 기준 80% 이상.
- 우선 확인 지점:
  - cgo 호출 횟수와 per-message allocation을 확인한다.
  - goroutine scheduling과 poller 대기 구조가 active 구간을 깎아 먹지 않는지
    확인한다.
  - native handle finalizer가 측정 경로에 영향을 주지 않도록 ownership을
    명확히 둔다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

### 9.7 Rust

- 목표: C 기준 90% 이상.
- 우선 확인 지점:
  - safe wrapper가 불필요한 copy, allocation, dynamic dispatch를 hot path에
    넣는지 확인한다.
  - ownership과 lifetime을 타입으로 지키되, send/recv 경로는 C API 의미에
    가깝게 유지한다.
  - panic/Result 변환과 error mapping이 active loop 안에서 반복되지 않는지
    확인한다.
- 완료 조건:
  - single + multi full matrix에서 목표 미달 조합이 없다.

## 10. 검증 게이트

변경 라운드마다 최소한 아래 게이트를 지난다.

1. 관련 회귀 테스트 통과.
2. 대상 언어 single smoke 통과.
3. 대상 언어 multi smoke 통과.
4. 수정한 라이브러리 범위가 core이면 `core/build` runtime 재빌드 후 C smoke 통과.
5. 목표 조합 재측정에서 개선 확인.
6. 최종 full single + full multi에서 목표 비율 충족.

권장 smoke 명령은 아래와 같다.

```bash
bindings/<lang>/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64 --runs 1
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64 --runs 1
```

core를 바꾼 경우에는 C 기준도 함께 확인한다.

```bash
cmake --build core/build
bindings/c/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64 --runs 1
bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64 --runs 1
```

## 11. 완료 정의

언어 하나의 완료 조건은 아래 모두를 만족하는 것이다.

- 해당 언어의 single + multi full matrix가 `complete`다.
- 정책에 정의된 조합을 `UNSUPPORTED`나 `SKIP`으로 숨기지 않았다.
- C 기준과 같은 조합에서 목표 비율을 만족한다.
- throughput을 올리기 위해 latency triplet을 비정상적으로 악화시키지 않았다.
- 발견한 binding/core 버그는 회귀 테스트와 함께 수정했다.
- perf 자체를 수정했다면, 그 수정 이유가 측정 버그였고 정책 의미를 바꾸지 않았음을
  결과 로그에 남겼다.

전체 작업의 완료 조건은 C++ → .NET → Java → Node → Python → Go → Rust 순서로
위 조건을 모두 통과하고, 마지막에 같은 날짜 기준의 전체 결과 요약을 남기는 것이다.

## 12. 최종 요약 양식

전체 작업이 끝나면 아래 표를 채운다.

| 언어 | 목표 | single 결과 | multi 결과 | 최종 기준 파일 | 비고 |
|------|------|-------------|------------|----------------|------|
| C++ | 90% | 미측정 | 미측정 | 미정 | 미정 |
| .NET | 85% | 미측정 | 미측정 | 미정 | 미정 |
| Java | 85% | 미측정 | 미측정 | 미정 | 미정 |
| Node | 70% | 미측정 | 미측정 | 미정 | 미정 |
| Python | 70% | 미측정 | 미측정 | 미정 | 미정 |
| Go | 80% | 미측정 | 미측정 | 미정 | 미정 |
| Rust | 90% | 미측정 | 미측정 | 미정 | 미정 |
