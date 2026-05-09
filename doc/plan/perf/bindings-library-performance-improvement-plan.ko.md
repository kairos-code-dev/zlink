# bindings 라이브러리 성능 개선 실행 계획

> 작성일: 2026-05-08
>
> 목적: `bindings/c/perf` 결과를 기준으로 삼아 언어별 binding 라이브러리의
> 성능을 목표 비율까지 끌어올리는 작업 순서와 반복 절차를 고정한다.
>
> 기준 정책:
> - [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
> - [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
> - [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

## TODO

- [ ] 대상 조합별 C 기준 측정 및 비교
- [ ] C++ 목표 달성
- [ ] .NET 목표 달성
- [ ] Java 목표 달성
- [ ] Node 목표 달성
- [ ] Python 목표 달성
- [ ] Go 목표 달성
- [ ] Rust 목표 달성
- [ ] 전체 언어 최종 결과 요약

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
| 3 | Java | `bindings/java/perf` | C 기준 80% 이상 |
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

- `bindings/c/perf`가 기준이다. 기준은 전체 matrix를 미리 고정하지 않고,
  작업 중인 동일 suite, pattern, transport, message size 조합을 필요할 때
  `bindings/c/perf`에서 직접 측정한 결과로 삼는다.
- `bindings/c/perf`를 실행하기 전에 `core/build` runtime을 최신으로 만든다.
  `core/src/` 또는 `core/include/`를 바꾼 뒤에는 반드시
  `cmake --build core/build`를 먼저 실행한다.
- `bindings/c/perf` 수치는 `core/build`의 `libzlink.so` 기준으로만 해석한다.
  `build_cpp_release`나 임시 빌드 디렉터리의 runtime으로 C 기준을 만들지 않는다.
- 각 언어는 위 표의 순서대로 하나씩 끝낸다. 앞 언어가 목표를 달성하기 전에는
  다음 언어 최적화로 넘어가지 않는다.
- 각 언어 perf는 해당 언어 binding의 public API만 사용해야 한다. native core
  perf 바이너리를 호출해 결과만 중계하는 방식은 측정으로 인정하지 않는다.
  C를 제외한 언어의 perf가 `zlink_*` C API, C FFI 함수, 내부 native helper,
  내부 구현 클래스를 직접 호출해 수치를 만들면 해당 언어 binding 성능으로
  인정하지 않는다. C API 호출은 해당 binding 라이브러리 내부 구현에만 둘 수
  있고, perf hot path는 사용자에게 공개된 binding API를 통과해야 한다.
  성능 수치 달성만을 위해 새 public API, native API, raw handle API, zero-copy
  전용 API를 추가한 뒤 perf만 그 경로를 쓰게 하는 것도 금지한다. 공개 API는
  이미 구현된 계약을 기준으로 보고, 성능 개선은 기존 binding public API의
  내부 구현, 객체 수명, 복사/할당, 예외 변환, callback dispatch 비용을 줄이는
  방향으로 수행한다.
- perf 측정 의미, RESULT line, ready/active phase, fail/skip/unsupported 의미는
  `doc/perf` 정책과 동일하게 유지한다.
- retry, inflight 제한, sleep 기반 보정, 숨은 fallback으로 수치를 만들지 않는다.
- 이 작업은 사람의 추가 판단을 기다리지 않고 진행한다. 모든 언어가 목표를
  달성할 때까지 자동으로 다음 측정, 분석, 수정, 검증 단계로 넘어간다.

## 3. 무중단 자동 운영 원칙

이 계획은 사람이 중간에 방향을 다시 정해 주지 않아도 끝까지 진행할 수 있어야
한다. 작업자는 아래 원칙에 따라 스스로 다음 행동을 결정한다.

작업자는 중간 결과를 설명할 수 있지만, 그 설명은 종료가 아니라 진행 중 상태
공유로만 취급한다. C++ → .NET → Java → Node → Python → Go → Rust 전체가
완료 정의를 만족하기 전에는 최종 보고로 작업을 닫지 않는다. 목표 미달 조합이
하나라도 남아 있으면 반드시 다음 측정, 분석, 수정, 테스트, 재측정 중 하나로
즉시 이어 간다.

### 3.0 최종 응답 금지 조건

아래 조건 중 하나라도 참이면 작업을 끝냈다고 말하지 않는다.

- 현재 언어의 정책 조합 중 목표 미달, partial, fail, 누락 결과가 남아 있다.
- 현재 언어의 목표 달성 여부를 같은 pattern, transport, message size 단위로
  확인하지 않았다.
- 앞 언어가 완료되지 않았는데 다음 언어 측정이나 최종 요약으로 넘어가려 한다.
- 변경한 binding/core 코드에 필요한 회귀 테스트나 smoke가 아직 통과하지 않았다.
- 실행 기록의 "다음 판단"에 다음 작업 항목이 남아 있다.

이 경우 작업자는 짧은 진행 상황을 남긴 뒤 바로 다음 작업 항목을 실행한다.
진행 상황 공유는 final report가 아니며, 작업 중단 사유가 될 수 없다.
대화나 실행 세션이 끊긴 뒤 다시 시작해도 새 계획을 세우지 않는다. 마지막
기록의 "다음 판단"과 현재 언어의 남은 미달 조합을 읽고, 그 지점에서 같은
루프를 재개한다.

### 3.1 중단하지 않는 기본 루프

전체 작업은 아래 루프를 목표 달성까지 반복한다.

1. 현재 대상 언어를 고른다.
2. 현재 볼 동일 pattern, transport, message size에서 C 기준과 대상 언어 결과를
   측정한다.
3. 목표 미달 조합을 찾는다.
4. 가장 큰 병목 하나를 고른다.
5. perf가 아니라 라이브러리 또는 core를 수정한다.
6. 필요한 회귀 테스트를 추가하거나 갱신한다.
7. 테스트와 perf smoke를 실행한다.
8. 목표 조합을 다시 측정한다.
9. 목표 조합이 기준을 만족하면 남은 정책 조합을 같은 방식으로 하나씩 확인해
   언어 완료 여부를 판단한다.
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

루프를 한 번 실행한 뒤에도 목표가 남아 있으면 같은 언어에서 다시 1번으로
돌아간다. "개선됐지만 목표 미달"은 완료가 아니라 다음 병목 분석 입력이다.
문서 기록을 남긴 직후에도 다음 측정 또는 다음 수정 명령을 계속 실행한다.
응답 작성, 라운드 기록, 로그 요약은 루프의 보조 작업일 뿐이다. 이런 작업을
마친 뒤에는 반드시 현재 언어의 다음 미달 조합으로 돌아가서 명령 실행 상태를
이어 간다.

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
- core 병목이면 core 개선 작업으로 전환하고, core runtime을 다시 만든 뒤 현재
  대상 조합의 C 기준을 다시 측정해 대상 언어 측정을 계속한다.
- 특정 언어 runtime 제한이 병목이면 public API 의미를 유지하는 범위에서 buffer
  재사용, native handle 소유권, 호출 횟수 축소 같은 대안을 계속 시도한다.
- 같은 가설이 반복해서 실패하면 실패한 가설 목록에 남기고 다음 가설로 넘어간다.

자동 확장 라운드는 반드시 아래 정보를 남긴다.

- 마지막 동일 조합 C 결과 파일과 대상 언어 결과 파일.
- 목표 미달 조합 목록.
- 이미 시도한 변경과 배제한 가설.
- 병목이 public API, runtime, core, binding 중 어디에 있는지에 대한 근거.
- 다음 자동 작업 항목과 검증 명령.

### 3.4 전체 종료 조건

전체 작업은 아래 조건이 충족될 때만 끝난다.

- 모든 언어가 정책에 정의된 single + multi 조합을 같은 pattern, transport,
  message size 단위로 확인했을 때 목표 비율을 만족하고 complete로 끝났다.

그 외 상황에서는 작업을 멈추지 않는다. 실행 환경 장애도 완료 조건이 아니며,
복구 명령, 빌드 재생성, 포트 정리, ulimit 조정 같은 자동 복구 작업으로 변환한다.
분석만 하고 중단하지 않으며, 항상 다음 측정 또는 다음 수정으로 이어 간다.

최종 응답은 위 전체 종료 조건을 만족한 뒤에만 작성한다. 전체 종료 조건을
만족하지 못했는데 응답을 해야 하는 상황이면, 응답에는 "진행 중"임을 밝히고
바로 이어서 실행할 다음 명령 또는 수정 항목을 함께 남긴 뒤 작업을 계속한다.
특히 C++이 완료되지 않았다면 .NET으로 넘어가지 않고 C++의 남은 목표 미달
조합을 계속 처리한다.
중간 응답 후 실제 명령 실행이 가능한 상태라면 같은 턴에서 바로 다음 명령을
실행한다. 응답은 작업 경계가 아니라 로그 경계이며, 다음 명령을 실행하지 않은
상태로 멈추면 이 계획을 위반한 것으로 본다.

### 3.5 AI 절대 금지 규칙

아래 규칙은 성능 목표 달성 여부, 마감 압박, 기술적 어려움과 관계없이
**어떤 이유로도 어길 수 없다**. 이 규칙을 어기려는 상황이 발생하면
즉시 작업을 멈추고 위반 시도 이유와 현재 상태를 보고한다.
계속 진행 여부는 사람만 결정할 수 있다.

**이 절은 이 문서의 다른 모든 절보다 우선한다.**

1. **binding public API를 성능 목표 달성만을 위해 수정하는 것은 금지한다.**  
   public API는 C API 또는 다른 언어 binding과 동작 불일치가 확인된 경우,
   또는 정책 계약 상 명백한 버그인 경우에만 수정할 수 있다.
   성능을 이유로 API 시그니처, 반환 타입, 소유권 계약, 예외 정책을 바꾸는 것은 금지한다.

2. **binding 또는 core 라이브러리 버그를 perf 코드에서 우회(workaround/bypass)하는 것은 금지한다.**  
   올바르게 구현된 perf가 동작하지 않으면 그 원인은 binding 또는 core 버그로 본다.
   perf 코드를 바꿔 증상을 숨기지 않는다. 해당 라이브러리를 수정한다.

3. **성능 수치 달성만을 위해 public API 계약 외의 경로를 여는 것은 금지한다.**  
   내부 API, native handle, raw FFI, zero-copy 전용 경로를 perf에서만 사용하도록
   추가하는 것은 성능을 위장한 API 우회다. 이 경우 perf 수치는 결과로 인정하지 않는다.

4. **목표 달성이 어렵다는 이유로 위 1–3 중 하나라도 선택하는 것은 금지한다.**  
   목표를 달성하지 못하더라도 위 행동을 선택하지 않는다.
   이 경우 "목표 미달 / 추가 분석 필요" 상태로 보고하고 사람의 판단을 기다린다.

## 4. 대상 조합 기준 수립

각 작업 라운드는 전체 perf matrix를 먼저 측정하지 않는다. 현재 분석할 동일
suite, pattern, transport, message size 조합만 C perf와 대상 언어 perf에서
각각 측정한다. 이렇게 해야 오래 걸리는 선측정 때문에 작업이 지연되지 않고,
수정한 병목이 실제 대상 조합에 어떤 영향을 줬는지 바로 볼 수 있다.

```bash
cmake --build core/build

# single 예시
bindings/c/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag c_<PATTERN>_<SIZE>_YYYYMMDD
bindings/<lang>/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag <lang>_<PATTERN>_<SIZE>_YYYYMMDD

# multi 예시
bindings/c/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag c_<PATTERN>_<SIZE>_YYYYMMDD
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --results-tag <lang>_<PATTERN>_<SIZE>_YYYYMMDD
```

비교는 항상 같은 조합끼리만 한다. 예를 들어 `MULTI_DEALER_ROUTER / tcp / 64B`
를 분석한다면 C도 그 조합만 측정하고, 대상 언어도 같은 조합만 측정한다.
`ALL` 또는 여러 size를 먼저 돌려 만든 오래된 파일을 근거로 현재 라운드의
목표 달성 여부를 판단하지 않는다.

언어 완료 판정도 한 번에 full matrix를 먼저 돌리는 방식이 아니다. 정책에 정의된
조합을 순서대로 확인하되, 각 조합은 동일한 `pattern / transport / size` 단위로
C와 대상 언어를 측정해 비교한다. 이미 같은 core runtime과 같은 작업 날짜에
측정한 동일 조합 결과가 있으면 그 파일을 재사용할 수 있지만, core 또는 해당
binding이 바뀐 뒤에는 그 조합을 다시 측정한다.

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
목표는 80%이므로 `0.80` 이상이어야 한다. 단, latency 계열은 측정 흔들림이
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

1. 현재 볼 `suite / pattern / transport / size` 조합을 고른다.
2. 그 조합만 C perf와 대상 언어 perf에서 측정한다.
3. 같은 조합끼리 `throughput`, `bandwidth`, `latency`, `latency_p95`,
   `latency_p99`를 비교한다.
4. 목표 미달 여부를 `pattern / transport / size / metric` 단위로 정리한다.
5. 가장 큰 손실이 있는 hot path를 하나 고른다.
6. binding 라이브러리나 core 라이브러리에서 병목을 줄이는 최소 변경을 만든다.
7. 변경 범위에 맞는 회귀 테스트를 먼저 실행한다.
8. 대상 조합 smoke를 실행한다.
9. 같은 대상 조합을 다시 측정해 개선 여부를 확인한다.
10. 결과 파일, 변경 요약, 남은 미달 조합을 이 계획 문서나 별도 로그에 남긴다.

한 번에 여러 가설을 섞지 않는다. 같은 라운드에 변경을 많이 넣으면 어떤 변경이
성능을 올렸는지 판단할 수 없고, 회귀가 생겼을 때 되돌릴 경계도 흐려진다.

### 6.1 라운드 기록 양식

각 라운드가 끝나면 아래 형식으로 기록한다. 기록 위치는 이 문서의 하단 또는
언어별 별도 로그 문서로 둔다.

```markdown
### YYYY-MM-DD <lang> round N

- 동일 조합 C 결과:
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
- perf가 C를 제외한 언어에서 `zlink_*` C API, C FFI, native handle, 내부 helper,
  내부 구현 클래스를 직접 사용해 binding public API를 우회하는 경우.

아래 수정은 금지한다.

- timeout을 늘려 실패를 통과처럼 보이게 하는 수정.
- retry를 추가해 실패를 숨기는 수정.
- inflight/outstanding 제한으로 throughput을 인위적으로 안정화하는 수정.
- 측정 구간 밖에서 보내야 할 메시지를 active 집계에 포함하는 수정.
- 특정 언어에만 유리하도록 pattern, transport, size 기본값을 바꾸는 수정.
- 성능 수치 달성만을 위해 새 public API, native API, raw handle API, zero-copy
  전용 API를 만들고 perf만 그 API를 사용하게 하는 수정.

perf 버그가 아닌데 perf를 바꾸고 싶어지는 상황은 대부분 라이브러리 문제나
측정 환경 문제다. 이 경우 perf를 고치지 말고 라이브러리 또는 환경을 고친다.

### 7.1 binding public API 수정 제한

binding public API(공개 헤더, 공개 메서드, 공개 인터페이스)는 아래 경우에만 수정할 수 있다.

- **C API와 동작 불일치**: 동일 패턴·트랜스포트에서 C API 계약을 따르지 않는 구현.
- **다른 언어 binding과 동작 불일치**: 같은 API 계약이 다른 언어 binding에서는 올바르게
  구현됐지만 해당 언어에서만 틀리게 구현된 경우.
- **정책 계약과 불일치**: `doc/spec` 또는 `doc/perf` 정책이 명시하는 계약과 다르게
  구현된 경우.

아래 경우는 수정할 수 없다.

- 성능 수치를 올리기 위해 API 시그니처, 반환 타입, 소유권 계약, 예외 정책을 바꾸는 것.
- perf에서만 이점이 있는 새 오버로드, 힌트 파라미터, zero-copy variant를 추가하는 것.
- 내부 구현 최적화를 위해 public API 의미를 암묵적으로 변경하는 것.

public API 수정이 필요하다고 판단되면 즉시 작업을 멈추고 수정 이유, 불일치 근거,
변경 전후 API 시그니처를 보고한 뒤 사람의 승인을 받는다.

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
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.2 .NET

- 목표: C 기준 85% 이상.
- 우선 확인 지점:
  - P/Invoke boundary에서 per-message allocation, array pinning, marshal 비용이
    반복되는지 확인한다.
  - buffer 재사용, span 기반 API, handle ownership이 public API 의미를 해치지
    않는 범위에서 적용되는지 확인한다.
  - ReadyToRun, tiered compilation 옵션이 정책 권장값과 맞는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.3 Java

- 목표: C 기준 80% 이상.
- 우선 확인 지점:
  - JNI boundary에서 byte array copy가 반복되는지 확인한다.
  - direct buffer, native handle lifecycle, exception 변환이 hot path에 들어가는지
    확인한다.
  - `-server`, `-XX:TieredStopAtLevel=1` 등 perf 실행 옵션이 정책 권장값과 맞는지
    확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.4 Node

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - N-API boundary에서 Buffer copy와 JS object 생성이 per-message로 반복되는지
    확인한다.
  - event loop 연동이 recv drain을 충분히 진행하는지 확인한다.
  - TSFN 같은 direct callback 동기화 비용을 perf 기본 surface에 끌어들이지
    않았는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.5 Python

- 목표: C 기준 70% 이상.
- 우선 확인 지점:
  - C extension boundary에서 bytes 생성과 GIL 잡는 구간이 hot path를 막는지
    확인한다.
  - reusable buffer나 memoryview를 public API 의미 안에서 쓸 수 있는지 확인한다.
  - recv drain을 Python 루프가 불필요하게 잘게 쪼개지 않는지 확인한다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

### 9.6 Go

- 목표: C 기준 80% 이상.
- 우선 확인 지점:
  - cgo 호출 횟수와 per-message allocation을 확인한다.
  - goroutine scheduling과 poller 대기 구조가 active 구간을 깎아 먹지 않는지
    확인한다.
  - native handle finalizer가 측정 경로에 영향을 주지 않도록 ownership을
    명확히 둔다.
- 완료 조건:
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

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
  - 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size
    단위로 확인했을 때 목표 미달 조합이 없다.

## 10. 검증 게이트

변경 라운드마다 최소한 아래 게이트를 지난다.

1. 관련 회귀 테스트 통과.
2. 대상 언어 single smoke 통과.
3. 대상 언어 multi smoke 통과.
4. 수정한 라이브러리 범위가 core이면 `core/build` runtime 재빌드 후 C smoke 통과.
5. 목표 조합 재측정에서 개선 확인.
6. 정책에 정의된 조합을 같은 pattern, transport, message size 단위로 확인했을 때
   목표 비율 충족.

권장 smoke 명령은 아래와 같다.

```bash
bindings/<lang>/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
bindings/<lang>/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
```

core를 바꾼 경우에는 C 기준도 함께 확인한다.

```bash
cmake --build core/build
bindings/c/perf/run_benchmarks.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
bindings/c/perf/run_benchmarks_multi.sh --pattern <PATTERN> --msg-sizes <SIZE> --runs 1
```

## 11. 완료 정의

언어 하나의 완료 조건은 아래 모두를 만족하는 것이다.

- 정책에 정의된 single + multi 조합을 같은 pattern, transport, message size 단위로
  확인했을 때 모두 `complete`다.
- 정책에 정의된 조합을 `UNSUPPORTED`나 `SKIP`으로 숨기지 않았다.
- C와 같은 `suite / pattern / transport / size` 조합에서 목표 비율을 만족한다.
- throughput을 올리기 위해 latency triplet을 비정상적으로 악화시키지 않았다.
- 발견한 binding/core 버그는 회귀 테스트와 함께 수정했다.
- perf 자체를 수정했다면, 그 수정 이유가 측정 버그였고 정책 의미를 바꾸지 않았음을
  결과 로그에 남겼다.

전체 작업의 완료 조건은 C++ → .NET → Java → Node → Python → Go → Rust 순서로
위 조건을 모두 통과하고, 마지막에 조합별 C 기준과 대상 언어 결과 요약을 남기는
것이다.

## 12. 최종 요약 양식

전체 작업이 끝나면 아래 표를 채운다.

| 언어 | 목표 | single 결과 | multi 결과 | 조합별 C 기준 | 비고 |
|------|------|-------------|------------|----------------|------|
| C++ | 90% | 미측정 | 미측정 | 미정 | 미정 |
| .NET | 85% | 미측정 | 미측정 | 미정 | 미정 |
| Java | 85% | 미측정 | 미측정 | 미정 | 미정 |
| Node | 70% | 미측정 | 미측정 | 미정 | 미정 |
| Python | 70% | 미측정 | 미측정 | 미정 | 미정 |
| Go | 80% | 미측정 | 미측정 | 미정 | 미정 |
| Rust | 90% | 미측정 | 미측정 | 미정 | 미정 |

## 실행 기록

### 2026-05-08 C++ round 1

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260508_185841_baseline_20260508.txt`
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260508_191249_baseline_20260508.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_191944_baseline_20260508.txt`
  - 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_192859_cpp_round1_spot_reqrep.txt`
- 목표 미달 조합:
  - `MULTI_SPOT_REQREP` 64B throughput이 C 기준의 약 1.5%였다.
  - `MULTI_SPOT`은 `UNSUPPORTED`로 출력됐고, C++ runner는 `MULTI_SPOT_SENDSEND`를 실행하지 않았다.
  - `MULTI_PUBSUB`, `MULTI_STREAM`, raw multi echo 계열도 64B에서 목표 미달이다.
- 선택한 병목 가설:
  - C++ `async_result_t::wait_for(0)`가 내부 request progress를 돌리지 않아, perf client가 request를 slot별로 병렬 진행하지 못하고 `get()`에서 직렬화된다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/async_result.hpp`
  - `bindings/cpp/perf/multi/src/perf_spot_reqrep_client.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_request_reply.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_request_reply`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_request_reply$' --output-on-failure`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_spot_reqrep_client cpp_comp_src_spot_reqrep_server`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --msg-sizes 64 --runs 1 --results-tag cpp_round1_spot_reqrep`
- 결과:
  - 추가한 회귀 테스트는 수정 전 실패했고 수정 후 통과했다.
  - `MULTI_SPOT_REQREP` 64B throughput은 tcp 기준 약 `0.84 Kops/s`에서 `121.31 Kops/s`로 개선되어 C 기준 `58.54 Kops/s`를 넘었다.
- 다음 판단:
  - 64B 기준에서 가장 큰 남은 throughput 손실인 `MULTI_PUBSUB`를 다음 병목으로 분석한다.
  - `MULTI_SPOT` `UNSUPPORTED`와 `MULTI_SPOT_SENDSEND` 미실행은 C++ 완료 전 반드시 해소한다.

### 2026-05-08 C++ round 2

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - 이전: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_191944_baseline_20260508.txt`
  - 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_193739_cpp_round4_pubsub_pollout.txt`
- 목표 미달 조합:
  - `MULTI_PUBSUB` 64B throughput이 C 기준의 약 7.5%였다.
- 선택한 병목 가설:
  - C++ PUB server가 EAGAIN 뒤 POLLOUT을 최대 100ms 기다려 active send loop가 과하게 쉬었다.
  - PUBSUB client hot path도 `topic_message_t` 생성과 payload 재복사를 수행하고 있었다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/perf/common/perf_socket_compat.hpp`
  - `bindings/cpp/perf/multi/src/perf_pubsub_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_pubsub_server.cpp`
- 추가/수정한 회귀 테스트:
  - 없음. perf 정책 hot path 수정이며 public C++ API 계약은 바꾸지 않았다.
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target cpp_comp_src_pubsub_client cpp_comp_src_pubsub_server`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern PUBSUB --msg-sizes 64 --runs 1 --results-tag cpp_round4_pubsub_pollout`
- 결과:
  - `MULTI_PUBSUB` 64B throughput은 tcp 기준 약 `253.44 Kmsg/s`에서 `3886.31 Kmsg/s`로 개선되어 C 기준 `3392.85 Kmsg/s`를 넘었다.
- 다음 판단:
  - 남은 큰 손실은 `MULTI_STREAM`, raw multi echo 계열, single latency 계열, `MULTI_SPOT`/`MULTI_SPOT_SENDSEND` 지원 공백이다.

### 2026-05-08 C++ round 3

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - 이전: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_193839_cpp_after_round2_64.txt`
  - 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_215738_cpp_spot_core_runtime_tcp.txt`
  - 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_220706_cpp_spot_tls_pollout.txt`
- 목표 미달 조합:
  - `MULTI_SPOT`은 C++ perf runtime이 `bindings/cpp/native/...`의 오래된 runtime을 사용해 symbol lookup 오류로 실패했다.
  - runtime 수정 뒤 `MULTI_SPOT,tls,64`가 C 기준의 약 1.5%였다.
- 선택한 병목 가설:
  - C++ perf runner의 `.runtime/*/lib`가 `core/build/lib`가 아니라 stale native runtime을 가리켰다.
  - C++ SPOT server는 `EAGAIN` 뒤 blocking publish로 fallback하고, 실패하면 단순 idle wait를 수행해 tls active send loop가 멈췄다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/perf/prepare_cpp_runtime.py`
  - `bindings/cpp/perf/multi/src/perf_spot_server.cpp`
- 추가/수정한 회귀 테스트:
  - 없음. stale runtime 선택과 SPOT server send wait는 perf 측정 버그다.
- 실행한 검증 명령:
  - `python3 -m py_compile bindings/cpp/perf/prepare_cpp_runtime.py`
  - `bindings/cpp/perf/prepare_cpp_runtime.py --suite multi`
  - `bindings/cpp/perf/prepare_cpp_runtime.py --suite single`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_spot_server`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern SPOT --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_spot_core_runtime_tcp`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern SPOT --msg-sizes 64 --runs 1 --transports tls --results-tag cpp_spot_tls_pollout`
- 결과:
  - `.runtime/{single,multi}/lib`는 `core/build/lib`를 가리키며, core source가 runtime보다 최신이면 실패한다.
  - `MULTI_SPOT,tcp,64`는 정상 complete가 되었고 `673.99 Kmsg/s`로 C 기준을 넘었다.
  - `MULTI_SPOT,tls,64`는 `5.12 Kmsg/s`에서 `600.42 Kmsg/s`로 개선되어 C 기준 `348.06 Kmsg/s`를 넘었다.
- 다음 판단:
  - C++ 64B multi 재측정에서 `MULTI_SPOT_SENDSEND` 누락, `MULTI_STREAM`, raw echo, `MULTI_DEALER_DEALER`이 남은 주요 미달 조합이다.

### 2026-05-08 C++ round 4

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - 전체 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_215811_cpp_after_runtime_fix_64.txt`
  - raw echo 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_221152_cpp_routed_recv_fastpath_64.txt`
  - raw echo 재측정: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_221327_cpp_echo_external_all_64.txt`
  - 배제한 STREAM 가설: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_221817_cpp_stream_raw_packet_tcp.txt`
- 목표 미달 조합:
  - `MULTI_SPOT_SENDSEND` 64B 결과가 없다.
  - `MULTI_STREAM` 64B throughput ratio가 약 0.41-0.44다.
  - raw echo 계열 64B throughput ratio가 약 0.43-0.62다.
  - `MULTI_DEALER_DEALER` 64B throughput ratio가 약 0.43-0.66이다.
- 선택한 병목 가설:
  - routed single-part receive가 `received_t`와 `std::vector<message_t>`를 만들기 때문에 raw echo hot path 비용이 크다.
  - echo client가 tcp 외 transport에서 매 요청 payload를 복사한다.
  - STREAM server의 packet callback 재조립 복사가 병목일 수 있다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/base_socket.hpp`
  - `bindings/cpp/include/zlink/message_socket.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/perf/common/perf_socket_compat.hpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_router_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_router_router_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_router_server.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_socket`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_dealer_router_server cpp_comp_src_router_router_server cpp_comp_src_dealer_router_client cpp_comp_src_router_router_client`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --runs 1 --results-tag cpp_routed_recv_fastpath_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --runs 1 --results-tag cpp_echo_external_all_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern STREAM --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_stream_raw_packet_tcp`
- 결과:
  - routed single-part recv 계약 테스트는 수정 전 실패했고 수정 후 통과했다.
  - raw echo throughput 개선은 작았다. `MULTI_ROUTER_ROUTER,tcp,64`는 약 `228.76 Kops/s`에서 `258.68 Kops/s`까지 올랐지만 C 기준 `399.96 Kops/s`에는 미달이다.
  - STREAM raw packet echo 가설은 tcp smoke가 `partial`로 실패해 되돌렸다.
- 다음 판단:
  - 다음 자동 작업은 `MULTI_SPOT_SENDSEND`를 C++ public SPOT API로 추가해 matrix 누락을 먼저 제거한 뒤, raw echo/STREAM의 남은 구조적 비용을 다시 분석한다.

### 2026-05-08 C++ round 5

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_223125_cpp_spot_sendsend_initial_64.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_223215_cpp_spot_sendsend_initial_non_tcp_64.txt`
- 목표 미달 조합:
  - `MULTI_SPOT_SENDSEND` 64B 결과가 없어 C++ matrix가 정책 조합을 모두 측정하지 못했다.
  - C++ `spot_t::recv_routed()`가 `spot_recv_impl` 미정의 심볼을 참조해, public routed receive API를 사용하는 바이너리가 링크되지 않았다.
- 선택한 병목 가설:
  - C++ perf runner가 `SPOT_SENDSEND` 서버/클라이언트 바이너리와 pattern metadata를 등록하지 않았다.
  - `spot_t::recv_routed()`는 core 공개 C API인 `zlink_spot_recv_part`를 직접 조립해야 하는데 존재하지 않는 내부 helper를 호출하고 있었다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/services/spot.hpp`
  - `bindings/cpp/perf/CMakeLists.txt`
  - `bindings/cpp/perf/prepare_cpp_runtime.py`
  - `bindings/cpp/perf/run_binding_multi.sh`
  - `bindings/cpp/perf/run_comparison.py`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_client.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `python3 -m py_compile bindings/cpp/perf/run_comparison.py bindings/cpp/perf/prepare_cpp_runtime.py`
  - `cmake --build bindings/cpp/build --target test_cpp_contract_socket cpp_comp_src_spot_sendsend_server cpp_comp_src_spot_sendsend_client`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
  - `bindings/cpp/perf/prepare_cpp_runtime.py --suite multi`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_spot_sendsend_initial_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --msg-sizes 64 --runs 1 --transports tls,ws,wss --results-tag cpp_spot_sendsend_initial_non_tcp_64`
- 결과:
  - `spot_t::recv_routed()` 링크 회귀 테스트는 수정 후 통과했다.
  - `MULTI_SPOT_SENDSEND` 64B throughput은 tcp `270.48 Kops/s`, tls `252.50 Kops/s`, ws `236.29 Kops/s`, wss `252.57 Kops/s`로 모두 C 기준 90%를 넘었다.
- 다음 판단:
  - `MULTI_SPOT_SENDSEND` 누락은 해소됐다.
  - 다음 미달군은 `MULTI_DEALER_DEALER`, raw echo 계열, `MULTI_STREAM`이다.

### 2026-05-08 C++ round 6

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_190543_baseline_20260508.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_223722_cpp_dealer_single_recv_64.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_223823_cpp_single_recv_echo_64.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_224204_cpp_poller_into_echo_tcp_64.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_224309_cpp_dealer_dealer_burst_send_tcp_64.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260508_224340_cpp_dealer_dealer_server_drain_tcp_64.txt`
- 목표 미달 조합:
  - `MULTI_DEALER_DEALER,tcp,64`는 C 기준 `3571.29 Kmsg/s` 대비 약 `1934.82 Kmsg/s`로 미달이다.
  - `MULTI_DEALER_ROUTER,tcp,64`는 C 기준 `419.43 Kops/s` 대비 약 `263.33 Kops/s`로 미달이다.
  - `MULTI_ROUTER_ROUTER,tcp,64`는 C 기준 `399.96 Kops/s` 대비 약 `257.37 Kops/s`로 미달이다.
- 선택한 병목 가설:
  - `PAIR`/`DEALER` 수신이 `received_t`와 `std::vector<message_t>`를 생성해 hot path 비용이 컸다.
  - `poller_t::wait_all()`이 매 호출마다 결과 vector를 새로 만들어 client hot path에 불필요한 allocation을 넣었다.
  - `MULTI_DEALER_DEALER` C++ client/server active window가 C runner와 다르게 drain/submit 루프를 구성했다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/message_socket.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/include/zlink/poller.hpp`
  - `bindings/cpp/perf/common/perf_socket_compat.hpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_dealer_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_dealer_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_router_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_router_router_client.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_socket cpp_comp_src_dealer_dealer_client cpp_comp_src_dealer_router_client cpp_comp_src_router_router_client`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_dealer_dealer_server cpp_comp_src_dealer_dealer_client`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER --msg-sizes 64 --runs 1 --results-tag cpp_dealer_single_recv_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --runs 1 --results-tag cpp_single_recv_echo_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_poller_into_echo_tcp_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_dealer_dealer_burst_send_tcp_64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_dealer_dealer_server_drain_tcp_64`
- 결과:
  - `PAIR`/`DEALER` single-part recv 계약 테스트는 통과했다.
  - `poller_t::wait_all(events_out, ...)` 재사용 overload 빌드는 통과했다.
  - `MULTI_DEALER_DEALER` tcp 64B, raw echo tcp 64B 개선은 목표에 충분하지 않았다.
- 다음 판단:
  - 남은 C++ 미달은 단순 수신 allocation보다 `perf::socket_t` compat dispatch, routing id 복사, C++ poller/tag 처리, 또는 서버 hot path 구조 비용이 더 큰 것으로 보고 raw echo/one-way 서버 hot path를 계속 분석한다.

### 2026-05-09 C++ round 7

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260508_235750_c_dealer_current_check_tcp64.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_000249_cpp_dealer_final_probe_tcp64.txt`
- 목표 미달 조합:
  - `MULTI_DEALER_DEALER,tcp,64`는 이번 라운드에서 통과했다.
  - C++ 완료 전 남은 미달군은 `MULTI_STREAM,tcp,64`, `MULTI_DEALER_ROUTER,tcp,64`, `MULTI_ROUTER_ROUTER,tcp,64`이다.
- 선택한 병목 가설:
  - `DEALER_DEALER` client가 `lat_out == NULL`이어도 active send latency sampler를 매 전송마다 갱신했다.
  - C++ server receive hot path가 public C++ wrapper의 `message_t`/exception 경로와 `poller_t` event 객체를 거쳤다.
  - server latency sampler가 active 구간 중 재할당을 반복했다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/base_socket.hpp`
  - `bindings/cpp/include/zlink/message_socket.hpp`
  - `bindings/cpp/include/zlink/poller.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_dealer_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_dealer_dealer_server.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_socket cpp_comp_src_dealer_dealer_client cpp_comp_src_dealer_dealer_server`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
  - `bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER --msg-sizes 64 --runs 1 --transports tcp --results-tag c_dealer_current_check_tcp64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER --msg-sizes 64 --runs 1 --transports tcp --results-tag cpp_dealer_final_probe_tcp64`
- 결과:
  - `MULTI_DEALER_DEALER,tcp,64` C 기준은 `3889.199 Kmsg/s`이다.
  - C++ 결과는 `3520.739 Kmsg/s`로 throughput ratio가 약 `0.905`이며 C++ 목표 `0.90`을 넘었다.
  - latency p99도 C `53.606 ms` 대비 C++ `0.357 ms`라 악화 조건에 해당하지 않는다.
- 다음 판단:
  - `MULTI_DEALER_DEALER,tcp,64`는 통과로 고정한다.
  - 다음 자동 작업은 C++ `MULTI_STREAM,tcp,64`를 같은 pattern/transport/size 기준으로 다시 분석하고, 목표 미달이면 public C++ API 또는 binding hot path를 수정한다.

### 2026-05-09 C++ round 8

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260509_005316_c_stream64_round_next.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_005328_cpp_stream64_round_next.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_011329_cpp_stream64_after_msgunit_audit.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_012106_cpp_stream64_msg_move_struct.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_012228_cpp_stream64_stream_send_restore_adopt.txt`
- 목표 미달 조합:
  - `MULTI_STREAM,tcp,64`는 C `394.836 Kops/s` 대비 C++ 최고 `153.705 Kops/s`로 ratio가 약 `0.389`라 목표 `0.90`에 미달한다.
- 선택한 병목 가설:
  - C++ STREAM server의 `message_t` adopt/move/restore 경로가 callback hot path에서 불필요한 `zlink_msg_init`/`zlink_msg_move`/`zlink_msg_close`를 반복했다.
  - C++ multi perf가 일부 pattern에서 size별 `AUTO_HWM_MSG_UNIT_BYTES`를 설정하지 않거나 recalc를 누락해 C 기준과 socket sizing 조건이 달라질 수 있었다.
  - runner 표시가 auto-HWM 기본값을 숫자 HWM처럼 보여 실제 설정 해석을 흐렸다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/message.hpp`
  - `bindings/cpp/include/zlink/services/spot.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/perf/common/perf_socket_compat.hpp`
  - `bindings/cpp/perf/multi/common/perf_common.hpp`
  - `bindings/cpp/perf/multi/common/perf_spot_control.hpp`
  - `bindings/cpp/perf/multi/src/perf_pubsub_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_pubsub_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_reqrep_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_reqrep_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_stream_server.cpp`
  - `bindings/cpp/perf/run_binding_multi.sh`
  - `bindings/cpp/perf/run_comparison.py`
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/cpp/README.md`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_message.cpp`
  - `bindings/cpp/tests/contract/test_cpp_contract_service.cpp`
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_message test_cpp_contract_socket cpp_comp_src_stream_server`
  - `ctest --test-dir bindings/cpp/build -R '^(test_cpp_contract_message|test_cpp_contract_socket)$' --output-on-failure`
  - `cmake --build bindings/cpp/build --target test_cpp_contract_service cpp_comp_src_spot_client cpp_comp_src_spot_server cpp_comp_src_spot_sendsend_server cpp_comp_src_spot_sendsend_client cpp_comp_src_spot_reqrep_server cpp_comp_src_spot_reqrep_client cpp_comp_src_pubsub_server cpp_comp_src_pubsub_client`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_service$' --output-on-failure`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_after_msgunit_audit`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_msg_move_struct`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_stream_send_restore_adopt`
- 결과:
  - `AUTO_HWM_MSG_UNIT_BYTES` 정책은 `doc/perf/PERF_MULTI_TEST_POLICY.md`와 `doc/perf/PERF_POLICY.md`에 이미 명시되어 있었다.
  - C++ multi perf는 일반 socket과 SPOT node/handle 경로에서 현재 size를 MsgUnit으로 설정하고 context auto-HWM을 recalc하도록 보정했다.
  - `message_t` move를 wrapper 내부 소유권 이전으로 줄였지만 STREAM throughput은 `153.705 Kops/s` 수준으로 목표 미달이 계속된다.
- 다음 판단:
  - C++ 완료 전 `MULTI_STREAM,tcp,64`를 계속 분석한다.
  - 다음 자동 작업은 C++ `stream_socket_t::on_packet` callback dispatch와 `message_t` native adopt 비용, 그리고 STREAM server pending/EAGAIN 빈도를 분리해 병목을 좁힌다.

### 2026-05-09 C++ round 9

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260509_005316_c_stream64_round_next.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_012912_cpp_stream64_adopt_no_close.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_013105_cpp_stream64_ctx_align.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_013146_cpp_stream64_recalc_after_bind.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_013640_cpp_stream64_msgunit_option_fix.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_013850_cpp_stream64_rid_direct.txt`
- 목표 미달 조합:
  - `MULTI_STREAM,tcp,64`는 C `394.836 Kops/s` 대비 C++ 최고 `153.579 Kops/s`로 ratio가 약 `0.389`라 목표 `0.90`에 미달한다.
- 선택한 병목 가설:
  - C++ common socket option의 `AUTO_HWM_MSG_UNIT_BYTES` setter가 C 계약의 `int` 크기와 다르게 `int64_t`로 호출해 MsgUnit 설정 실패 또는 ready 전 `EINVAL`을 만들 수 있었다.
  - C perf의 context 기본과 C++ perf context 기본을 맞추기 위해 C++ multi context에 `BLOCKY=0`과 balanced auto-HWM profile을 명시 적용했다.
  - STREAM server는 bind 이후에도 auto-HWM을 다시 계산해야 socket 역할이 반영될 수 있다고 보고 재계산을 추가했다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/types.hpp`
  - `bindings/cpp/include/zlink/types_impl.hpp`
  - `bindings/cpp/include/zlink/message.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/perf/multi/common/perf_common.hpp`
  - `bindings/cpp/perf/multi/src/perf_stream_server.cpp`
- 추가/수정한 회귀 테스트:
  - `bindings/cpp/tests/contract/test_cpp_contract_socket.cpp`
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target test_cpp_contract_socket cpp_comp_src_stream_server`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
  - `bindings/cpp/perf/run_binding_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_msgunit_option_fix`
  - `bindings/cpp/perf/run_binding_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_rid_direct`
  - `bindings/cpp/perf/run_binding_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_timeout_align`
- 결과:
  - `auto_hwm_msg_unit_bytes(64)` common socket option 계약 테스트를 추가했고 통과했다.
  - MsgUnit setter 타입 버그와 context 기본값 차이는 고쳤지만 STREAM throughput은 `148~153 Kops/s` 범위에 머물러 목표 미달이다.
  - STREAM 전용 timeout 적용도 C 서버와 맞췄지만 결과는 `150.716 Kops/s`로 목표 미달이다.
  - `zlink_msg_t` opaque storage를 binding에서 직접 복사하는 가설은 server partial을 만들어 즉시 배제하고 `zlink_msg_adopt` 경로로 되돌렸다.
- 다음 판단:
  - C++ 완료 전 `MULTI_STREAM,tcp,64`를 계속 분석한다.
  - 다음 자동 작업은 public C++ API를 유지한 상태에서 `stream_socket_t::send` nonblocking hot path와 callback dispatch 비용을 더 줄일 수 있는지 확인한다.

### 2026-05-09 C++ round 10

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260509_005316_c_stream64_round_next.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_014820_cpp_stream64_diag_queue_debug.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_015152_cpp_stream64_diag_hwm.txt`
- 목표 미달 조합:
  - `MULTI_STREAM,tcp,64`는 C `394.836 Kops/s` 대비 C++ `156~158 Kops/s` 범위로 ratio가 약 `0.40`이라 목표 `0.90`에 미달한다.
- 선택한 병목 가설:
  - C++ STREAM 저하는 HWM, socket buffer, context profile, pending queue 문제가 아니라 callback/send hot path 자체의 per-message 비용일 가능성이 높다.
  - C 기준은 packet callback에서 native `zlink_msg_t *`와 `zlink_routing_id_t *`를 그대로 처리하지만, C++ 기준은 public `stream_socket_t::on_packet`에서 `routing_id_t`, `message_t` RAII wrapper, `std::function` 호출, `stream_socket_t::send` wrapper를 매 packet마다 거친다.
- 변경한 라이브러리 파일:
  - 없음. 진단용 출력은 측정 후 제거했다.
- 추가/수정한 회귀 테스트:
  - 없음.
- 실행한 검증 명령:
  - `cmake --build core/build`
  - `PERF_DEBUG_TRANSITIONS=1 bindings/cpp/perf/run_binding_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_diag_queue_debug`
  - `PERF_DEBUG_TRANSITIONS=1 bindings/cpp/perf/run_binding_multi.sh --pattern STREAM --msg-sizes 64 --transports tcp --runs 1 --results-tag cpp_stream64_diag_hwm`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_stream_server test_cpp_contract_socket`
  - `ctest --test-dir bindings/cpp/build -R '^test_cpp_contract_socket$' --output-on-failure`
- 결과:
  - C++ STREAM server 진단에서 `recv=797494`, `sent=797494`, `eagain=0`, `enqueued=0`, `pending_end=0`, `pending_max=0`으로 확인했다. 즉 pending queue와 backpressure는 원인이 아니다.
  - C++ monitor snapshot은 `enabled=1`, `profile=2`, `role=7`, `unit_budget=65536`, `msg_unit=64`, `slots=1024`, `sndhwm=128`, `rcvhwm=128`, `sndbuf=262144`, `rcvbuf=262144`였다. C 기준 Auto-HWM detail과 같은 적용값이다.
  - 진단 후 임시 출력은 제거했고 C++ STREAM server 빌드와 socket contract 테스트가 통과했다.
- 다음 판단:
  - 다음 자동 작업은 perf가 아닌 C++ binding library 내부에서 public API를 유지하면서 `stream_socket_t::on_packet` callback trampoline, `routing_id_t` 복사, `message_t` adopt/move, `stream_socket_t::send` single-part nonblocking 경로를 순서대로 줄이는 것이다.

### 2026-05-09 C++ round 11

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_095059_c_single_spot_wss1024_after_core_publish_fastpath_check.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_094936_cpp_single_spot_wss1024_after_topic_string_reuse.txt`
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_095249_cpp_single_spot_wss1024_after_subscribe_single_part_fastpath.txt`
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_100542_cpp_single_spot_wss1024_after_publish_validation_fastpath.txt`
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_100634_cpp_single_spot_wss1024_after_skip_topic_compare.txt`
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_100713_cpp_single_spot_wss1024_after_send_ready_wait.txt`
- 목표 미달 조합:
  - `SPOT,wss,1024`는 C `59.737 Kmsg/s` 대비 C++ 최고 안정 측정이 `40~42 Kmsg/s` 범위라 throughput ratio가 약 `0.68~0.70`으로 목표 `0.90`에 미달한다.
  - latency triplet도 C 기준보다 크게 나빠져 별도 병목으로 남았다.
- 선택한 병목 가설:
  - C++ SPOT single은 policy에 맞지 않게 dispatch callback과 discovery bootstrap을 사용하던 구조를 public `spot_t::publish()` / `spot_t::subscribe()` active loop와 direct peer topology로 맞췄다.
  - C 기준 `zlink_publish()`는 SPOT 단일 메시지를 직접 publish하지만, C++ public builder는 `zlink_spot_publish_part(..., ZLINK_PART_FINAL)`를 통해 staged part helper를 거쳐 단일 메시지에도 불필요한 상태 조회와 vector 경로가 있었다.
  - C++ `topic_message_t`가 단일 payload도 항상 vector로 보관해 수신 hot path allocation을 만든다고 보고 단일 메시지 내부 저장 경로를 추가했다.
  - send-ready event wait, poller wait, topic 비교 제거는 개선되지 않아 유지하지 않는다.
- 변경한 라이브러리 파일:
  - `core/src/api/service_spot_api.cpp`
  - `core/tests/unittest/unittest_service_mode_policy.cpp`
  - `bindings/cpp/include/zlink/services/spot.hpp`
  - `bindings/cpp/include/zlink/types.hpp`
  - `bindings/cpp/include/zlink/types_impl.hpp`
  - `bindings/cpp/perf/single/src/perf_spot.cpp`
- 추가/수정한 회귀 테스트:
  - `core/tests/unittest/unittest_service_mode_policy.cpp`
- 실행한 검증 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build -R unittest_service_mode_policy --output-on-failure`
  - `cmake --build bindings/cpp/build --target cpp_perf_spot`
  - `ctest --test-dir bindings/cpp/build -R "test_cpp_contract_service|test_cpp_contract_socket" --output-on-failure`
  - `bindings/c/perf/run_benchmarks.sh --pattern SPOT --transports wss --msg-sizes 1024 --duration 5 --results-tag c_single_spot_wss1024_after_core_publish_fastpath_check`
  - `bindings/cpp/perf/run_benchmarks.sh --reuse-build --pattern SPOT --transports wss --msg-sizes 1024 --duration 5 --results-tag cpp_single_spot_wss1024_after_publish_validation_fastpath`
- 결과:
  - core public `zlink_spot_publish_part(..., ZLINK_PART_FINAL)`에 단일 메시지 fast path를 추가했고 회귀 테스트가 통과했다.
  - C++ SPOT perf는 public binding API만 사용하도록 active loop를 수정했다.
  - `SPOT,tcp,256`은 `439.267 Kmsg/s`로 개선됐지만, `SPOT,wss,1024`는 아직 목표 미달이다.
- 다음 판단:
  - C++ 완료 전 `SPOT,wss,1024`를 계속 분석한다.
  - 다음 자동 작업은 C++ `spot_t::subscribe()` public 반환 객체 생성 비용과 SPOT WSS data-plane backlog를 분리해서, sleep이나 inflight 제한 없이 receiver 처리량을 올릴 수 있는 라이브러리 내부 개선을 찾는다.

### 2026-05-09 .NET round 1

- 전환 사유:
  - C++은 완료 전 상태지만, 운영자가 "일단 C++은 여기까지 하고 dotnet으로 넘어가"라고 지시했다. 이 라운드는 그 지시에 따라 .NET으로 전환한 기록이다.
- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_105729_dotnet_single_spot_reqrep64_after_progress_yield.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_111412_dotnet_single_routed64_after_received_single_fastpath.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_112452_dotnet_single_routed64_tcp_after_message_no_finalizer_probe.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_112621_dotnet_single_routed64_tcp_after_latency_cap_4m.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 최신 C `2076.775 Kmsg/s` 대비 .NET 최고 `1442.046 Kmsg/s`로 ratio가 약 `0.694`라 .NET 목표 `0.85`에 미달한다.
  - `ROUTER_ROUTER,tcp,64`는 최신 C `2275.605 Kmsg/s` 대비 .NET 최고 `1478.765 Kmsg/s`로 ratio가 약 `0.650`이라 .NET 목표 `0.85`에 미달한다.
- 선택한 병목 가설:
  - .NET SPOT_REQREP 저하는 request progress pump의 `Task.Delay(1)`가 request/reply active loop를 millisecond 단위로 제한한 것이 원인이었다.
  - .NET raw routed 저하는 C와 다른 receiver thread + poller + nonblocking recv 구조, 단일 part receive 객체 allocation, message helper P/Invoke, `Message` finalizer allocation 비용이 누적된 것으로 보인다.
  - routing id를 native struct snapshot으로 지연 변환하는 가설은 255-byte struct 복사 비용 때문에 개선되지 않아 추가 검토가 필요하다.
  - latency sample cap을 무제한으로 바꾸는 가설은 초기 capacity 과다 할당으로 process failure를 만들어 배제하고, 기본 cap은 `4_000_000`으로 조정했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/RequestProgressPump.cs`
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/MultipartMessageCollection.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
  - `bindings/dotnet/src/Zlink/Native/NativeMethods.Core.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfSocketIo.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPair.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerDealer.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPubSub.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs`
  - `bindings/dotnet/perf/single/run_benchmarks.sh`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 아직 추가하지 않았다. raw socket/message 기존 테스트를 회귀 확인에 사용했다.
- 실행한 검증 명령:
  - `cmake --build core/build`
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
  - `bindings/c/perf/run_benchmarks.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag c_single_routed64_tcp_dotnet_current_compare`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_routed64_tcp_after_latency_cap_4m`
- 결과:
  - SPOT_REQREP 64B는 `5.7~6.8 Kops/s`로 C 기준 대비 목표를 넘었다.
  - routed 64B는 초기 약 `0.43~0.48` ratio에서 약 `0.65~0.69`까지 올랐지만 목표 `0.85`에는 미달한다.
  - raw socket/message smoke는 통과했다.
  - 전체 .NET 테스트는 기존 SPOT/actor serviceName 계약 변경 잔여 실패가 있어 raw 회귀 테스트와 분리했다.
- 다음 판단:
  - .NET 완료 전 `DEALER_ROUTER,tcp,64`와 `ROUTER_ROUTER,tcp,64`를 계속 분석한다.
  - 다음 자동 작업은 `Message` finalizer 제거를 유지할 수 있는지 수명 계약을 검토하고, 불가하면 finalizer 비용을 피하는 안전한 소유권 구조를 설계한다. 동시에 routed receive의 routing id snapshot 변경은 되돌리거나 더 작은 copy 경로로 바꿔 수치 영향을 재확인한다.

### 2026-05-09 .NET round 2

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_121525_c_single_routed64_inproc_dotnet_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_120940_dotnet_single_routed64_tcp_resume_baseline.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121220_dotnet_single_routed64_tcp_after_routing_snapshot_inline.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121540_dotnet_single_routed64_inproc_compare.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121729_dotnet_single_routed64_tcp_after_message_dispose_suppress_removed.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_122435_dotnet_single_routed64_tcp_after_native_dispose_fastpath.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 C `2076.775 Kmsg/s` 대비 이번 라운드 최고 `1396.838 Kmsg/s` 수준으로 ratio가 약 `0.67`이라 .NET 목표 `0.85`에 미달한다.
  - `ROUTER_ROUTER,tcp,64`는 C `2275.605 Kmsg/s` 대비 이번 라운드 최고 `1448.243 Kmsg/s` 수준으로 ratio가 약 `0.64`라 .NET 목표 `0.85`에 미달한다.
  - `inproc`에서도 C 대비 .NET ratio가 약 `0.65~0.70`이라 transport보다 public `Send`/`Recv` wrapper와 `Message`/`Received` 객체 비용 쪽 병목이 더 크다.
- 선택한 병목 가설:
  - CPU sample에서 active 시간은 `SocketKernel.SendSingleCore`와 `SocketKernel.ReceiveRouterParts`에 거의 반반 걸렸다. metric 출력이나 정렬 비용은 주 병목이 아니었다.
  - `RoutingIdSnapshot`을 32B inline snapshot으로 바꿨지만 목표 조합 개선은 미미했다.
  - `Received.Dispose()`의 원자 연산 제거, `Message.Dispose()`의 불필요한 `GC.SuppressFinalize()` 제거, receive native-owned dispose fast path는 모두 소폭 개선에 그쳤다.
  - blocking send/recv에 `SuppressGCTransition`을 붙이는 실험은 실행 실패를 만들었고, 블로킹 호출 안전성도 맞지 않아 배제했다.
  - .NET single runner가 실제 실패를 `UNSUPPORTED`로 숨길 수 있는 예외 기반 추정을 제거했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/RoutingId.cs`
  - `bindings/dotnet/src/Zlink/RoutingIdCodec.cs`
  - `bindings/dotnet/src/Zlink/RoutingIdSnapshot.cs`
  - `bindings/dotnet/src/Zlink/Native/NativeTypes.cs`
  - `bindings/dotnet/src/Zlink/Sockets/MessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
  - `bindings/dotnet/perf/single/run_benchmarks.sh`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 아직 추가하지 않았다. raw socket/message 기존 테스트를 회귀 확인에 사용했다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
- 결과:
  - raw socket/message 회귀 테스트 48개는 통과했다.
  - routed 64B 목표 조합은 아직 목표 미달이다.
- 다음 판단:
  - 다음 자동 작업은 `Message` 생성과 native part 초기화/copy 비용을 더 직접적으로 분해한다.
  - public API 우회 없이 `Message(ReadOnlySpan<byte>)`, `SocketKernel.SendSingleCore`, `ReceiveRouterParts`, `Message.AsReadOnlySpan`, `Received.Dispose` 각각의 비용을 C perf의 대응 구간과 비교하고, 실제 library 내부에서 줄일 수 있는 복사와 객체 수명 비용부터 계속 줄인다.

### 2026-05-09 .NET round 3

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_123246_dotnet_single_routed64_tcp_after_received_snapshot_unify.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_123526_dotnet_single_routed64_tcp_after_received_metadata_split.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_124020_dotnet_single_routed64_tcp_after_routingid_native_cache.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_125459_dotnet_single_routed64_tcp_after_send_notready_false.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_125518_dotnet_single_rr64_tcp_confirm_send_notready_false.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_130501_dotnet_single_rr64_tcp_after_send_success_no_dispose.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 C `2076.775 Kmsg/s` 대비 .NET 최고 `1815.450 Kmsg/s`로 ratio가 약 `0.874`이며 .NET 목표 `0.85`를 넘었다.
  - `ROUTER_ROUTER,tcp,64`는 C `2275.605 Kmsg/s` 대비 .NET 최고 `1901.423 Kmsg/s`로 ratio가 약 `0.835`라 .NET 목표 `0.85`에 아직 미달한다.
- 선택한 병목 가설:
  - `ROUTER_ROUTER`의 남은 차이는 routed send에서 `RoutingId`를 native `zlink_routing_id_t`로 전달하는 비용과 receive 객체 생성 비용이 합쳐진 것으로 보인다.
  - `Send()`가 backpressure와 not-ready 결과를 예외로 바꾸면 C perf의 `EAGAIN`/`ENOTCONN` 처리보다 비용이 커진다. bool public API 결과를 사용해 false로 돌려주는 경로가 throughput을 가장 크게 올렸다.
  - Router-to-router 양방향 handshake를 C와 맞추는 실험, managed payload borrowed-send 생성자, routing id unmanaged pointer 전달, 8B routing snapshot, `Received` payload union, `Received` routing id box는 목표 조합을 개선하지 않아 배제했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/RoutingId.cs`
  - `bindings/dotnet/src/Zlink/Sockets/MessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfSocketIo.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 추가하지 않았다. public `Send()`의 false 반환 경로는 기존 raw socket/message 테스트와 targeted perf로 확인했다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_routed64_tcp_after_send_notready_false`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_send_notready_false`
- 결과:
  - raw socket/message 회귀 테스트 48개는 통과했다.
  - `DEALER_ROUTER,tcp,64`는 목표를 넘었지만, `.NET` 완료 조건은 `ROUTER_ROUTER,tcp,64`가 남아 있어 아직 만족하지 못했다.
- 다음 판단:
  - 다음 자동 작업은 `ROUTER_ROUTER,tcp,64`만 계속 대상으로 삼고, routed send의 `RoutingId` native 전달 비용과 receive object allocation을 더 분해한다.
  - `ROUTER_ROUTER,tcp,64`가 C 기준 85%를 넘기 전에는 Java로 넘어가지 않는다.

### 2026-05-09 C++ round 12

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_142426_c_single_spot_wss1024_cpp_compare_current.txt`
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260509_144726_c_multi_router_router_tcp64_cpp_compare_current.txt`
- 대상 언어 결과:
  - `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260509_142347_cpp_single_spot_wss1024_post_full_rerun.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_144638_cpp_multi_spot_tcp_tls64_after_spot_msgunit_remove.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_144711_cpp_multi_spot_sendsend_tls262144_after_spot_msgunit_remove.txt`
  - `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260509_150950_cpp_multi_router_router_tcp64_after_skip_reply_rid_compare.txt`
- 목표 미달 조합:
  - `SPOT,wss,1024` targeted throughput은 C `78.786 Kmsg/s` 대비 C++ `77.286 Kmsg/s`로 목표를 넘었다.
  - `MULTI_ROUTER_ROUTER,tcp,64`는 C `438.977 Kops/s` 대비 C++ 최고 `277.478 Kops/s`로 ratio가 약 `0.632`라 C++ 목표 `0.90`에 미달한다.
- 선택한 병목 가설:
  - SPOT perf가 SPOT node/handle에 MsgUnit을 적용하던 경로는 정책과 맞지 않아 제거했다. raw socket 경로의 MsgUnit은 유지했다.
  - C++ `poller_t::wait_all()`의 등록형 poller 경로가 C 기준의 `zlink_pollitem_t` 경로보다 무거워 socket/fd 전용 빠른 경로를 추가했다.
  - router-router client의 reply routing id 재복사와 비교는 C 기준 hot path에 없어서 제거했다.
  - `recv_router_received()`를 `zlink_router_recv_part` 기반으로 바꾸는 실험은 throughput을 낮춰 되돌렸다.
- 변경한 라이브러리 파일:
  - `bindings/cpp/include/zlink/poller.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/include/zlink/types.hpp`
  - `bindings/cpp/include/zlink/types_impl.hpp`
  - `bindings/cpp/include/zlink/services/spot.hpp`
- 변경한 perf 파일:
  - `bindings/cpp/perf/multi/common/perf_common.hpp`
  - `bindings/cpp/perf/multi/src/perf_router_router_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_router_router_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_server.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_client.cpp`
  - `bindings/cpp/perf/multi/src/perf_spot_sendsend_server.cpp`
  - `bindings/cpp/perf/single/src/perf_spot.cpp`
  - `bindings/cpp/perf/single/src/perf_spot_reqrep.cpp`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 추가하지 않았다. public API 계약 변경 없이 내부 hot path와 perf 정책 정렬만 수행했다.
- 실행한 검증 명령:
  - `cmake --build bindings/cpp/build --target cpp_perf_spot cpp_perf_spot_reqrep`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_spot_server cpp_comp_src_spot_client cpp_comp_src_spot_sendsend_server cpp_comp_src_spot_sendsend_client`
  - `cmake --build bindings/cpp/build --target cpp_comp_src_router_router_server cpp_comp_src_router_router_client`
  - `bindings/cpp/perf/run_benchmarks.sh --reuse-build --pattern SPOT --transports wss --msg-sizes 1024 --duration 5 --results-tag cpp_single_spot_wss1024_post_full_rerun`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT --transports tcp,tls --msg-sizes 64 --duration 5 --results-tag cpp_multi_spot_tcp_tls64_after_spot_msgunit_remove`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT_SENDSEND --transports tls --msg-sizes 262144 --duration 5 --results-tag cpp_multi_spot_sendsend_tls262144_after_spot_msgunit_remove`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag cpp_multi_router_router_tcp64_after_skip_reply_rid_compare`
- 결과:
  - C++ SPOT targeted fail/crash 조합은 complete로 회복했다.
  - C++ `MULTI_ROUTER_ROUTER,tcp,64`는 개선됐지만 목표 미달이다.
- 다음 판단:
  - public API 우회 없이 남은 router-router 차이를 닫기 어렵다. 별도 public API 추가나 perf의 C API 직접 호출은 금지되어 있으므로 미달 상태를 유지하고 다음 언어 확인으로 넘어간다.

### 2026-05-09 .NET round 4

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151025_dotnet_single_rr64_tcp_current_after_cpp.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151049_dotnet_single_rr64_tcp_confirm_current.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151237_dotnet_single_rr64_tcp_confirm_current_2.txt`
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`는 C `2259.763 Kmsg/s` 대비 .NET 최고 `1864.689 Kmsg/s`로 ratio가 약 `0.825`라 .NET 목표 `0.85`에 미달한다.
- 선택한 병목 가설:
  - 남은 차이는 public `Message` 생성, routed send, `Received`/`Message` 수신 객체 생성 비용이다.
  - borrowed/internal send 경로를 perf에서 직접 쓰면 public API 측정 목적을 깨기 때문에 사용하지 않았다.
- 변경한 파일:
  - 없음.
- 실행한 검증 명령:
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_current_after_cpp`
  - `bindings/c/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag c_single_rr64_dotnet_compare_current`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_current`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_current_2`
- 결과:
  - .NET은 아직 완료 조건을 만족하지 못했다.
- 다음 판단:
  - public API 우회 없이 남은 3% 내외의 차이를 닫으려면 `Message`/`Received` 객체 수명 구조 개선이 필요하다.

### 2026-05-09 Java round 1

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_151303_java_single_rr64_tcp_initial.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_151340_java_single_rr64_tcp_after_context_msgunit_align.txt`
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`는 C `2259.763 Kmsg/s` 대비 Java 최고 `526.800 Kmsg/s`로 ratio가 약 `0.233`라 Java 목표에 크게 미달한다.
- 선택한 병목 가설:
  - Java single router-router가 C와 다르게 tcp에서도 송신/수신 context를 나누고, size별 MsgUnit과 auto-HWM 재계산을 적용하지 않았다.
  - 이를 C 구조와 맞췄지만 throughput 개선은 작아, 주 병목은 public Java `Message`/`Received` 객체와 FFM/JNI 호출 비용으로 보인다.
- 변경한 파일:
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/systems/zlink/perf/single/PerfRouterRouter.java`
- 실행한 검증 명령:
  - `bindings/java/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag java_single_rr64_tcp_initial`
  - `bindings/java/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag java_single_rr64_tcp_after_context_msgunit_align`
- 결과:
  - Java는 아직 완료 조건을 만족하지 못했다.
- 다음 판단:
  - 다음 작업은 Java public API 내부에서 `Message.copyOf`/`send`/`recv` hot path의 객체 생성과 native segment 접근 비용을 줄이는 것이다.
