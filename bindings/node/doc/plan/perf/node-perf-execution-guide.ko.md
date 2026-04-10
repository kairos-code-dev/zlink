# Node Perf 실행 가이드

> 상태: 진행중
> 기준 문서: 이 실행 가이드 하나로 고정
> 대상 범위: `bindings/node/perf/`, `bindings/node/tests/`, `bindings/node/package.json`, `bindings/node/perf/README.md`, `bindings/node/plan/perf/`
> 목적: `bindings/node/perf`가 [`bindings/README.md`](../../../../doc/spec/bindings/README.md#perf-policy) 633-795 line의 `Perf Policy`부터 `Perf Review Checklist`까지 전부 만족할 때까지 반복 실행 기준과 완료 판정 기준을 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 목적

이 문서는 `bindings/node/perf` 정렬 작업의 유일한 실행 authority다.

이번 loop의 목표는 아래 하나로 고정한다.

- Node perf surface가 [`bindings/README.md`](../../../../doc/spec/bindings/README.md#perf-policy) 633-795
  line의 perf 정책 전체를 실제 코드, 스크립트, 출력, 문서, 검증 경로에서
  만족하도록 끝까지 정렬한다.

실행 중 설계 판단이 필요하면 먼저 이 가이드를 고치고, 그 다음 코드를 수정한다.

## 2. 고정 기준

이번 실행에서 반드시 만족해야 하는 기준 문서는 아래다.

- [`bindings/README.md`](../../../../doc/spec/bindings/README.md#perf-policy)
- [`doc/perf/PERF_POLICY.md`](../../../../doc/perf/PERF_POLICY.md)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](../../../../doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](../../../../doc/perf/PERF_MULTI_TEST_POLICY.md)

완료 판정은 README 633-795 line의 항목을 아래 범주로 모두 충족했을 때만 가능하다.

- Perf Design Rules
- Perf Structure Rules
- Perf Alignment Rules
- Perf Script Interface Rules
- Perf Verification Requirements
- Perf Review Checklist

테스트나 smoke가 통과해도 위 범주 중 하나라도 미충족이면 완료가 아니다.

## 3. 범위와 비범위

이번 실행 범위:

- `bindings/node/perf/`
- `bindings/node/tests/`
- `bindings/node/package.json`
- `bindings/node/perf/README.md`
- `bindings/node/plan/perf/`

필요 시 함께 갱신 가능한 생성물:

- `bindings/node/dist-tools/perf/`

이번 실행 제외 범위:

- `core/`
- `core/tests/`
- `bindings/cpp/`
- `bindings/java/`
- `bindings/python/`

정책:

- `core` bug fix 요청이 아니므로 `core/`는 수정하지 않는다.
- perf 정렬의 근거를 위해 one-off 프로그램이나 `/tmp` 실험을 만들지 않는다.
- generated output이 필요한 경우 source를 먼저 수정하고 `bindings/node` 공식 build
  경로로 재생성한다.

## 4. 현재 기준선

현재 확인된 상태는 아래다.

- perf entrypoint는 존재한다.
  - `./bindings/node/perf/run_benchmarks.sh`
  - `./bindings/node/perf/run_benchmarks_multi.sh`
- single 구현 패턴은 `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `SPOT` 까지 있다.
- multi 구현 패턴은 `MULTI_DEALER_DEALER`, `MULTI_PUBSUB`, `STREAM`만 있다.
- 현재 runner 기본값은 README 요구값과 다르다.
  - single: duration `2`, warmup `1`, msg sizes `[256]`
  - multi: duration `2`, warmup `1`, msg sizes `[256]`
- `--help` 는 help를 출력하지 않고 실제 benchmark 코드 경로로 진입한다.
- multi runner는 `--clients`를 공용 CLI로 노출하지 않는다.
- single 구현은 실질적으로 `inproc` 한 가지 transport에 고정되어 있고,
  multi 구현은 `tcp` 중심으로만 동작한다.
- multi public pattern surface는 아직 core/perf 기본 범위
  (`DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT`,
  `STREAM`)와 맞지 않는다.
- 출력은 `RESULT` line과 effective options 일부는 내보내지만 core/perf와 같은
  markdown table, result section, completion summary contract까지는 아직 맞지 않는다.
- 결과 파일 경로와 파일명 prefix는 policy 방향에 가깝지만, 전체 script
  interface, coverage, verification alignment는 아직 완료가 아니다.

이 기준선은 코드가 바뀌면 같이 갱신한다.

## 5. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 이 가이드만으로 해결할 수 없는 Node perf public contract 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/node/` 범위를 넘는 blocker 확인

위 경우가 아니면 아래 순서로 반복한다.

1. 첫 미완료 slice를 잡는다.
2. code, docs, tests, generated output을 같이 정렬한다.
3. 관련 검증을 끝낸다.
4. 이 가이드 상태와 작업 레지스터를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

절대 멈추면 안 되는 경우:

- 일부 테스트만 통과했지만 README perf checklist가 남아 있는 상태
- entrypoint만 존재하지만 CLI/default/output contract가 core/perf와 다른 상태
- pattern file은 늘었지만 perf hot path 가시성 또는 문서화가 남은 상태

commit / push는 사용자 지시가 있을 때만 수행한다.

## 6. 자동 반복 실행

자동 반복 실행은 아래 wrapper 하나만 사용한다.

- [`run_node_perf_execution.sh`](./run_node_perf_execution.sh)

이 wrapper는 내부적으로 공통 supervisor인
[`core/tools/ralphloop/run_codex_execution_guide_loop.sh`](../../../../core/tools/ralphloop/run_codex_execution_guide_loop.sh)
를 호출한다.

Codex 최종 응답 계약:

- 모든 항목이 끝났을 때만 정확히 `미적용 사항이 없습니다.` 를 출력한다.
- 사용자 결정 없이는 진행할 수 없는 blocker가 있을 때만 `사용자 입력 필요: ...`
  형식 한 줄만 출력한다.
- 그 외에는 slice를 하나 끝낼 때마다 `계속 진행 필요`를 출력하고 다음 iteration으로
  넘어간다.

## 7. 기본 검증 명령

기본 검증:

```bash
cd bindings/node && npm run build

cd bindings/node && ./perf/run_benchmarks.sh --pattern PAIR --recv callback --duration 1 --warmup 1 --msg-sizes 64 --runs 1

cd bindings/node && ./perf/run_benchmarks_multi.sh --pattern STREAM --recv recv --duration 1 --warmup 1 --msg-sizes 64 --runs 1 --clients 2
```

CLI smoke:

```bash
cd bindings/node && ./perf/run_benchmarks.sh --help
cd bindings/node && ./perf/run_benchmarks_multi.sh --help
```

문서/스크립트 정렬 smoke:

```bash
./bindings/node/plan/perf/run_node_perf_execution.sh --init-only
```

완료 직전 검증:

```bash
cd bindings/node && npm run build

cd bindings/node && ./perf/run_benchmarks.sh --pattern ALL --recv callback --runs 1

cd bindings/node && ./perf/run_benchmarks.sh --pattern PUBSUB --recv callback --runs 1

cd bindings/node && ./perf/run_benchmarks_multi.sh --pattern ALL --recv recv --runs 1 --clients 2

cd bindings/node && ./perf/run_benchmarks_multi.sh --pattern STREAM --recv callback --runs 1 --clients 2
```

주의:

- duration/warmup은 완료 전에도 짧게 줄여 smoke할 수 있지만, CLI surface와 기본값은
  core/perf 기본값과 정확히 맞아야 한다.
- help 명령은 절대 benchmark를 실행하면 안 된다.
- 결과 파일과 stdout은 모두 policy-required structure를 유지해야 한다.

## 8. 작업 레지스터

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 8.1 Slice 0. 실행 authority와 반복 wrapper 고정

상태: `완료`

대상:

- `bindings/node/plan/perf/node-perf-execution-guide.ko.md`
- `bindings/node/plan/perf/run_node_perf_execution.sh`

작업:

- perf loop의 단일 authority 문서를 만든다.
- 종료 문구와 반복 규칙을 고정한다.
- wrapper smoke 경로를 제공한다.

완료 기준:

- perf loop가 README perf policy 전체 충족 전에는 종료되지 않는 규칙이 문서화된다.
- wrapper가 공통 supervisor를 호출하는 최소 smoke 경로를 가진다.

### 8.2 Slice 1. single/multi CLI 인터페이스를 core/perf와 동일하게 정렬

상태: `미착수`

대상:

- `bindings/node/perf/run_benchmarks.sh`
- `bindings/node/perf/run_benchmarks_multi.sh`
- `bindings/node/perf/single/run_benchmarks.ts`
- `bindings/node/perf/multi/run_benchmarks.ts`
- `bindings/node/perf/common/perf_metrics.ts`
- 필요 시 `bindings/node/package.json`

작업:

- `--pattern`, `--recv`, `--duration`, `--warmup`, `--msg-sizes`, `--transports`,
  `--runs`, `--results-dir`, `--results-tag`를 core/perf와 같은 의미와 기본값으로
  맞춘다.
- multi suite는 `--clients`를 추가하고 core/perf 기본값을 따른다.
- `--help`를 구현해 실제 실행 없이 usage를 출력한다.
- single 기본값을 duration `5`, warmup `2`, recv `callback`,
  msg sizes `64,256,1024,65536,131072,262144`로 맞춘다.
- multi 기본값을 duration `5`, warmup `2`, recv `recv`, clients `100`
  (STREAM `10000`) 및 message sizes policy와 맞춘다.

완료 기준:

- README의 Perf Script Interface Rules 중 CLI 옵션과 기본값 항목이 모두 충족된다.
- help output이 benchmark 실행 없이 끝난다.

### 8.3 Slice 2. single suite output/report/coverage 정렬

상태: `미착수`

대상:

- `bindings/node/perf/single/*.ts`
- `bindings/node/perf/common/*.ts`
- `bindings/node/perf/README.md`
- 필요 시 `bindings/node/tests/`

작업:

- single runner 출력 구조를 core/perf와 같은 `Effective Options`와 markdown table
  shape로 맞춘다.
- `RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>` 계약을 깨지 않도록
  report와 stdout을 정렬한다.
- single suite는 policy대로 `--recv callback` only surface를 유지하고, 허용되지
  않는 `--recv recv`는 즉시 fail 하도록 contract를 분명히 한다.
- single transport/패턴 범위가 README의 "core/perf와 비교 가능한 시나리오"를
  만족하는지 코드와 문서에서 함께 설명 가능하게 만든다.
- pattern별 파일 분리, hot path 가시성, doc 정렬을 다시 확인한다.

완료 기준:

- README의 Design / Structure / Alignment / Script Interface / Verification 요구가
  single suite에서 충족된다.
- single completion evidence가 README checklist로 설명 가능하다.

### 8.4 Slice 3. multi suite pattern/clients/output 정렬

상태: `미착수`

대상:

- `bindings/node/perf/multi/*.ts`
- `bindings/node/perf/common/*.ts`
- `bindings/node/perf/README.md`
- 필요 시 `bindings/node/tests/`

작업:

- multi 기본 패턴을 `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`,
  `SPOT`, `STREAM` 범위로 `core/perf`와 맞춘다.
- `--clients`와 pattern별 기본 client count를 적용한다.
- `STREAM` callback mode와 non-STREAM recv policy를 core/perf와 맞춘다.
- multi pattern 파일 분리와 public pattern naming을 README 기준으로 정리한다.
- stdout/report/effective options/markdown table을 multi suite도 동일하게 맞춘다.

완료 기준:

- README의 multi suite 요구사항이 전부 충족된다.
- `./perf/run_benchmarks_multi.sh --pattern ALL --recv recv --runs 1 --clients 2`
  경로가 정책 기준으로 동작한다.

### 8.5 Slice 4. 문서, 검증 경로, 최종 체크리스트 정리

상태: `미착수`

대상:

- `bindings/node/perf/README.md`
- `bindings/node/plan/perf/node-perf-execution-guide.ko.md`
- 필요 시 `bindings/node/tests/`

작업:

- 현재 지원 범위와 policy 예외를 README에 남기지 말고, 실제 구현 상태와 문서를 일치시킨다.
- 검증 명령을 실제 작동 경로로 정리한다.
- 이 가이드의 현재 기준선과 slice 상태를 최종 상태로 갱신한다.

완료 기준:

- README와 실행 경로가 실제 코드와 일치한다.
- 이 가이드의 모든 slice 상태가 `완료`다.

## 9. 최종 체크

아래 질문에 모두 `예`로 답할 수 있을 때만 종료한다.

- Node perf가 바인딩 레이어 비용을 직접 측정하고 있는가
- harness 복잡도가 핵심 비용을 가리지 않는가
- 패턴별 파일 분리가 유지되는가
- core/perf와 비교 가능한 시나리오와 측정 단위를 유지하는가
- CLI 옵션 이름, 기본값, 출력 포맷, 결과 파일 naming이 core/perf와 같은가
- single/multi entrypoint와 검증 문서가 실제로 동작하는가
- README 633-795 line의 모든 요구사항을 코드와 문서로 설명할 수 있는가
- `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
  `PERF_MULTI_TEST_POLICY.md`와 충돌하는 로컬 예외가 남아 있지 않은가

위 질문 중 하나라도 `아니오`면 최종 응답은 `계속 진행 필요`다.
