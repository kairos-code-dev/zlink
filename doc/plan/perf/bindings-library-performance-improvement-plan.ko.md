# bindings 라이브러리 성능 개선 계획

> 이 문서는 bindings 라이브러리 성능을 C 기준 대비 목표 비율까지 끌어올리기 위한
> 실행 계획이다. 이전 측정 기록은 이 문서에 보관하지 않고, 매 라운드의 결과 파일과
> 최종 요약만 별도로 남긴다.

## 1. 범위와 목표

이번 작업은 perf 자체를 빠르게 만드는 일이 아니라, 각 언어 binding 라이브러리가
public API를 통해 내는 실제 성능을 개선하는 일이다. perf는 `doc/perf` 정책에 맞게
이미 작성되어 있다고 보고, perf 자체의 측정 버그가 확인된 경우를 제외하면
수정하지 않는다.

목표 비율은 같은 suite, pattern, transport, message size, metric 조합에서
`bindings/c/perf` 결과를 기준으로 계산한다.

| 순서 | 언어 | perf 경로 |
|------|------|-----------|
| 1 | C++ | `bindings/cpp/perf` |
| 2 | .NET | `bindings/dotnet/perf` |
| 3 | Java | `bindings/java/perf` |
| 4 | Node | `bindings/node/perf` |
| 5 | Python | `bindings/python/perf` |
| 6 | Go | `bindings/go/perf` |
| 7 | Rust | `bindings/rust/perf` |

목표 비율은 message size별로 다르게 적용한다. 최근 full 측정에서는 큰 메시지가
항상 더 쉬운 목표가 아니며, copy, buffer 수명, routing, transport 비용 때문에
64KB 이상에서 오히려 C 대비 비율이 낮아지는 조합이 있었다. 아래 표는 측정 오차와
런타임 변동을 감안한 최소 통과 기준이다. 모든 패턴 중 최솟값 기준으로 판단하며,
특정 패턴에서 이 수치를 밑돌면 해당 size 목표를 달성하지 못한 것으로 본다.

| Size | C++ | .NET | Java | Rust | Go | Node | Python |
|------|-----|------|------|------|----|------|--------|
| 64B | >=30% | >=30% | >=20% | >=45% | >=25% | >=15% | >=8% |
| 256B | >=35% | >=25% | >=20% | >=45% | >=25% | >=15% | >=8% |
| 1024B | >=30% | >=45% | >=20% | >=45% | >=25% | >=18% | >=10% |
| 64KB | >=30% | >=10% | >=20% | >=40% | >=20% | >=15% | >=8% |
| 128KB | >=25% | >=10% | >=30% | >=40% | >=20% | >=12% | >=6% |
| 256KB | >=30% | >=10% | >=45% | >=40% | >=20% | >=12% | >=6% |

latency, latency_p95, latency_p99는 throughput 목표를 만족하더라도 C 대비 과도하게
악화되면 완료로 보지 않는다. 레이턴시 악화가 보이면 같은 조합을 다시 측정하고,
binding 내부 병목인지 perf 측정 오류인지 먼저 구분한다.

## 2. 고정 원칙

- 성능 개선 대상은 perf가 아니라 각 언어 binding 라이브러리다.
- perf는 버그가 있거나 `doc/perf` 원칙을 위배했을 때만 수정한다.
- binding perf는 `bindings/c/perf`와 같은 의미를 측정해야 한다.
- binding perf hot path는 해당 언어의 public API를 사용해야 한다.
- 내부 API, private API, native helper, C API 직접 호출로 수치를 만드는 방식은
  인정하지 않는다.
- 수치 달성만을 위해 perf 전용 public API나 zero-copy 우회 API를 추가하지 않는다.
- `doc/perf/PERF_POLICY.md`,
  `doc/perf/PERF_SINGLE_TEST_POLICY.md`,
  `doc/perf/PERF_MULTI_TEST_POLICY.md`를 항상 따른다.

## 3. 실행 방식

각 binding의 공식 perf 스크립트를 그대로 사용한다.

- single: `bindings/<lang>/perf/run_binding_single.sh`
- multi: `bindings/<lang>/perf/run_binding_multi.sh`
- C 기준: `bindings/c/perf/run_benchmarks.sh`,
  `bindings/c/perf/run_benchmarks_multi.sh`

스크립트에 설정된 기본값을 바꾸지 않는다. 비교 범위를 좁힐 때만
`--transports`와 `--patterns`로 특정 transport와 특정 pattern을 지정한다.

한 라운드는 아래 순서로 진행한다.

1. 같은 transport와 pattern을 C perf로 측정한다.
2. 같은 transport와 pattern을 대상 binding perf로 측정한다.
3. C 대비 비율을 계산한다.
4. 미달 조합의 병목을 binding 라이브러리에서 찾는다.
5. binding 라이브러리를 수정한다.
6. 같은 조합을 다시 측정한다.
7. 목표를 넘을 때까지 반복한다.

single과 multi는 같은 원칙으로 반복한다. 한 번에 전체 matrix를 돌리지 않고,
`--transports`와 `--patterns`로 조합을 좁혀 원인과 개선 효과를 확인한 뒤 다음
조합으로 이동한다.

언어별 작업은 순차로 진행한다. 현재 대상 언어의 모든 single/multi 대상 조합이
목표 비율을 만족한 뒤에만 다음 언어로 넘어간다. 특정 transport나 pattern 일부가
목표를 넘었더라도 미달 조합이 하나라도 남아 있으면 해당 언어 작업은 계속 진행한다.

## 4. 에이전트 운영

성능 개선은 한 번에 한 개 언어 binding을 대상으로 진행한다. 같은 언어 안에서는
측정, 병목 분석, 구현 검토를 나누어 병렬로 진행할 수 있지만, 서로 다른 언어를
동시에 개선 대상으로 삼지 않는다.

- 작업 에이전트 A: 현재 언어 binding의 측정과 C 대비 비율 계산 담당
- 작업 에이전트 B: 현재 언어 binding의 병목 분석과 라이브러리 수정 담당
- 감독 에이전트: 결과 검토, perf 원칙 위반 여부 확인, 다음 작업 지시 담당

감독 에이전트는 각 라운드마다 아래를 확인한다.

- C 기준과 binding 결과가 같은 suite, transport, pattern, size를 비교했는지
- perf 수정이 필요한 경우 실제 버그나 원칙 위반이 있는지
- 수정이 binding public API 내부 구현 개선인지
- 목표 비율을 모든 항목에서 만족했는지
- 미달 항목이 남아 있으면 다음 병목 후보와 재측정 조합이 명확한지

목표에 도달하지 못한 언어는 완료로 표시하지 않는다. 감독 에이전트는 미달 항목이
남아 있는 동안 작업 에이전트에게 계속 다음 측정과 수정 지시를 내린다. 현재 언어가
완료되면 `## 1. 범위와 목표`의 순서에 따라 다음 언어를 시작한다.

## 5. 완료 기준

아래 조건을 모두 만족하면 해당 언어 binding 작업을 완료한다.

- single과 multi의 대상 조합이 모두 목표 비율 이상이다.
- perf 결과가 `doc/perf` 정책과 `bindings/c/perf` 의미를 유지한다.
- perf 코드를 수정했다면 버그 또는 정책 위반 근거가 남아 있다.
- binding 라이브러리 변경에 필요한 테스트가 통과한다.
- 결과 파일 경로와 C 대비 비율 요약이 최종 보고에 포함된다.

모든 대상 언어가 완료되면 최종 요약에는 언어별 최저 비율, 남은 예외, 수정한 파일,
실행한 perf 명령을 함께 기록한다.
