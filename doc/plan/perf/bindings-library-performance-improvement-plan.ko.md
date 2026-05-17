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

C 기준은 core 내부 이론 성능이 아니라, `bindings/c/perf`가 public C API로 측정한
C binding 라이브러리의 일반적인 성능이다. 기준으로 삼는 C 결과는 같은 기본 옵션으로
실행한 최근 full 측정 결과여야 하며, 특정 실험이나 debug 재현을 위한 일회성 결과는
기준으로 쓰지 않는다. C 결과 자체가 비정상적으로 낮거나 높아 보이면 같은 조건으로
재측정해 일반적인 범위를 먼저 확인한다.

| 순서 | 언어 | perf 경로 |
|------|------|-----------|
| 1 | C++ | `bindings/cpp/perf` |
| 2 | .NET | `bindings/dotnet/perf` |
| 3 | Java | `bindings/java/perf` |
| 4 | Node | `bindings/node/perf` |
| 5 | Python | `bindings/python/perf` |
| 6 | Go | `bindings/go/perf` |
| 7 | Rust | `bindings/rust/perf` |

목표 비율은 size 하나로만 정하지 않는다. 최근 C++ full 비교에서는 size보다
pattern 차이가 더 컸다. 예를 들어 single `PAIR`, `PUBSUB`, `SPOT`은 C와 비슷하거나
더 빠른 조합이 많았지만, routed pattern인 `DEALER_ROUTER`, `ROUTER_ROUTER`는 일부
transport와 큰 메시지에서 크게 낮아졌다.

다만 `ROUTER_ROUTER` 또는 `MULTI_ROUTER_ROUTER`가 현재 특정 binding에서 낮게 나온
결과를 그대로 낮은 목표 기준으로 인정하지 않는다. 같은 suite, transport, size에서
C의 `ROUTER_ROUTER`와 `DEALER_ROUTER` 차이가 작다면 해당 binding도 그 차이에
가까워야 한다. 즉 routed router 성능은 C 대비 절대 비율뿐 아니라 같은 binding의
`DEALER_ROUTER` 대비 상대 비율로도 검증한다. C++ `MULTI_ROUTER_ROUTER`처럼
`MULTI_DEALER_ROUTER` 대비 과도하게 낮은 결과는 목표 완화 근거가 아니라 binding
라이브러리 병목 또는 버그 후보로 본다.

따라서 완료 판단은 pattern 그룹별 범위를 먼저 적용하고, size는 보조 기준으로 본다.
아래 표의 왼쪽 값은 최소 통과 기준이고, 오른쪽 값은 안정권 기준이다. 64KB 이상 큰
메시지는 같은 pattern 그룹 안에서 낮은 쪽 기준을 적용하고, 64B~1024B 작은 메시지는
높은 쪽 기준에 가까워지는 것을 목표로 한다.

Node와 Python은 별도 근거 없이 큰 차이를 두지 않는다. 두 binding 모두 동적
런타임과 native 경계 비용이 큰 그룹으로 보고 같은 목표 범위를 적용한다. Rust는
C++보다 높은 기준으로 두지 않는다. 둘 다 native binding 그룹으로 보며, public API
래퍼 비용을 감안하더라도 managed runtime binding보다는 높은 기준을 적용한다.

| Pattern 그룹 | 포함 pattern | C++/Rust | .NET/Java | Go | Node/Python |
|--------------|--------------|----------|-----------|----|-------------|
| 단순 one-way | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `MULTI_PUBSUB`, `MULTI_STREAM` | 80~90% | 63~73% | 53~63% | 35~43% |
| routed one-way | `DEALER_ROUTER`, `ROUTER_ROUTER` | 70~83% | 55~67% | 47~57% | 33~40% |
| multi routed echo | `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER` | 65~77% | 50~63% | 40~53% | 30~37% |
| SPOT 계열 | `SPOT`, `MULTI_SPOT`, `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND` | 75~90% | 60~70% | 50~60% | 33~40% |

`ROUTER_ROUTER` 계열은 추가로 아래 상대 기준을 적용한다.

- C의 `ROUTER_ROUTER / DEALER_ROUTER` 비율을 같은 suite, transport, size에서 계산한다.
- 대상 binding의 `ROUTER_ROUTER / DEALER_ROUTER` 비율이 C의 상대 비율보다 크게
  낮으면, 절대 목표 비율을 넘더라도 완료로 보지 않는다.
- 상대 비율 허용 오차는 측정 오차를 감안해 C++/Rust는 10%p, .NET/Java/Go는 15%p,
  Node/Python은 20%p로 본다.

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
- 버그가 확인되면 perf에서 우회하지 않고 버그를 먼저 수정한다.
- 버그 수정 전에는 해당 동작을 재현하는 회귀테스트를 먼저 작성한다.
- binding 버그이면 해당 언어 binding 라이브러리에서 수정한다.
- core 버그이면 core에서 수정한 뒤 `bindings/dev_sync_local_core_libs.sh`로
  bindings에 local core library를 다시 배포한다.
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
- 실행 중 새 이슈가 발견되었는지, 발견된 이슈가 테스트와 수정으로 닫혔는지
- 계획 문서가 현재 실행 방식, 판단 기준, 남은 이슈를 정확히 반영하는지

목표에 도달하지 못한 언어는 완료로 표시하지 않는다. 감독 에이전트는 미달 항목이
남아 있는 동안 작업 에이전트에게 계속 다음 측정과 수정 지시를 내린다. 현재 언어가
완료되면 `## 1. 범위와 목표`의 순서에 따라 다음 언어를 시작한다.

실행 중 문제가 발견되면 같은 라운드 안에서 원인을 리뷰하고, 회귀테스트 작성,
버그 수정, 재측정, 문서 갱신을 반복한다. 측정 실패, 기준 불일치, perf 정책 위반,
binding/core 버그, 목표 기준의 모호함이 모두 사라질 때까지 해당 언어 작업을
종료하지 않는다.

## 5. 완료 기준

아래 조건을 모두 만족하면 해당 언어 binding 작업을 완료한다.

- single과 multi의 대상 조합이 모두 목표 비율 이상이다.
- perf 결과가 `doc/perf` 정책과 `bindings/c/perf` 의미를 유지한다.
- perf 코드를 수정했다면 버그 또는 정책 위반 근거가 남아 있다.
- binding 라이브러리 변경에 필요한 테스트가 통과한다.
- 실행 중 발견된 이슈가 모두 리뷰되었고, 필요한 테스트와 수정이 끝났다.
- 이 문서가 실제 실행 절차와 판단 기준을 최신 상태로 반영한다.
- 결과 파일 경로와 C 대비 비율 요약이 최종 보고에 포함된다.

모든 대상 언어가 완료되면 최종 요약에는 언어별 최저 비율, 남은 예외, 수정한 파일,
실행한 perf 명령을 함께 기록한다.
