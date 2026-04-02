# Bindings Perf Ralph Execution Guide

이 문서는 `core/tools/bindings-perf/` 랄프 루프의 유일한 authority 문서다.
별도 main/master/gap/residual 문서는 만들지 않는다.

## 1. 목적

`/home/hep7/project/kairos/zlink/core/perf/baseline/` 의 core C API baseline
측정값을 기준으로, 선택된 바인딩 언어의 perf hot path 비효율을 줄여
언어별 목표 비율 이상으로 끌어올린다.

핵심 원칙:

- `perf` benchmark 의미와 core C API baseline 의미가 다르면 그 정합성은 수정한다.
- `perf` 수정은 `/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md`,
  `/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md`,
  `/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md`
  와 다르거나, `core/perf` 구현 의미와 다르거나, benchmark 자체 버그가 있는 경우만 허용한다.
- benchmark 숫자만 올리기 위해 `perf` 에서만 의미가 있는 코드나 측정 전용 shortcut을 넣으면 안 된다.
- 이 랄프루프의 목표는 `bindings/<lang>/` 라이브러리의 실제 성능 향상이지, perf 숫자만 좋아 보이게 만드는 것이 아니다.
- 성능 격차는 우선 `bindings/<lang>/` 라이브러리 내부 비효율 제거로 해결한다.
- bug가 확인되면 우회 코드를 작성하면 안 된다.
- `bindings/<lang>/` 라이브러리 버그면 해당 바인딩 라이브러리를 직접 수정한 뒤 개선 작업을 계속한다.
- `core` 계약 실패나 core 버그면 우회하지 말고 재현 근거와 함께
  `/home/hep7/project/kairos/zlink/core/doc/bug/` 아래에 `.md` bug report를 작성하고 대기한다.
- 효과 없는 실험, 의미 왜곡, 정책 위반 수정은 남기지 않는다.

## 2. 대상 범위

선택된 언어 목록은 환경 변수 `BINDINGS_PERF_LANGUAGES` 를 따른다.
값이 없으면 아래 전체 언어를 대상으로 본다.

- `cpp`
- `dotnet`
- `java`
- `rust`
- `go`
- `node`
- `python`

baseline 디렉터리는 `BINDINGS_PERF_BASELINE_DIR` 를 따른다.
값이 없으면 `/home/hep7/project/kairos/zlink/core/perf/baseline` 를 사용한다.

baseline report 파일은 `BINDINGS_PERF_BASELINE_FILE` 를 우선 사용한다.
값이 없으면 baseline 디렉터리 안에서 아래 우선순위로 하나를 고른다.

1. 가장 최신 `perf_*callback*.txt`
2. 없으면 가장 최신 `perf_*.txt`

선택된 baseline file 경로는 매 실행 시작 시 로그에 반드시 남긴다.
같은 비교 기준으로 재현이 필요하면 wrapper의 `--baseline-file <path>` 로
동일 report 파일을 명시적으로 고정한다.

## 3. 언어별 기본 목표 비율

사용자가 명시 override 하지 않으면 아래 기본 목표를 사용한다.

- `cpp`: `0.95`
- `dotnet`: `0.90`
- `java`: `0.90`
- `rust`: `0.95`
- `go`: `0.85`
- `node`: `0.75`
- `python`: `0.75`

override 규칙:

- wrapper가 `BINDINGS_PERF_TARGET_<LANG>` 환경 변수를 주면 그 값을 우선 사용한다.
- 새로운 언어가 추가됐는데 목표가 정의되지 않았으면 기본값 `0.80` 으로 시작한다.
- 사용자가 직접 비율을 지정하면 guide 안의 기본값보다 그 지시를 우선한다.

## 4. 작업 원칙

- 사용자 호칭은 항상 `팀장님` 으로 유지한다.
- 변경 범위는 우선 `bindings/<lang>/` 와 해당 언어의 `perf/` 정합성 범위에 한정한다.
- `perf/` 수정은 core baseline 의미와 비교 surface를 맞추는 정합성 수정일 때만 허용한다.
- `perf/` 안에만 의미가 있고 제품 동작에는 반영되지 않는 성능 전용 우회 코드는 금지한다.
- benchmark 결과를 좋게 보이게 만들기 위한 shortcut, 조건 완화, payload 축소, 샘플 수 왜곡은 금지한다.
- 반복 측정은 같은 조건으로 비교한다. 가능한 한 `tcp`, `callback`, baseline과 같은 warmup/duration을 유지한다.
- perf 측정 실행은 항상 한 번에 하나만 수행한다.
- 두 개 이상의 perf runner, build, test, benchmark를 병렬로 돌려 측정치를 오염시키면 안 된다.
- 다른 언어 perf run, 같은 언어의 다른 pattern run, 별도 build/test job도 active perf 측정과 겹치지 않게 순차 실행한다.
- pre-existing dirty state는 건드리지 않는다.
- core bug가 확인되면 `core/` 를 임의 수정하지 말고 bug report를 먼저 남긴다.

## 5. iteration 절차

선택된 각 언어에 대해 아래 순서를 반복한다.

1. 해당 언어 perf surface와 현재 결과 파일 구조를 확인한다.
2. core baseline과 비교 가능한 transport/pattern/size 조합을 식별한다.
3. perf 의미가 어긋난 부분이 있으면 먼저 그 정합성을 고친다.
4. 동일 의미 비교가 확보되면 바인딩 라이브러리 hot path 병목을 찾는다.
5. 작은 메시지와 큰 payload를 분리해서 원인을 찾는다.
6. 실제로 성능이 오른 변경만 남기고, 효과 없거나 회귀인 실험은 원복한다.
7. 부분 probe로 방향을 확인한 뒤 전체 대상 조합을 다시 측정한다.
8. 목표 비율 미달 항목이 남아 있으면 다음 병목으로 이어간다.

중요 해석 규칙:

- 작업 레지스터의 `completed` 또는 `완료` 표시는 "이전 실행에서 마지막으로 확인된 상태"일 뿐이다.
- 새 랄프 실행이 시작되면 선택된 언어는 모두 다시 baseline 대비 현재 상태를 재확인해야 한다.
- 즉 `completed` 언어라도 현재 workspace, baseline, perf runner, report가 바뀌었을 수 있으므로 skip 하면 안 된다.
- skip 이 허용되는 경우는 이번 실행에서 방금 같은 조건으로 재측정했고 목표 충족이 다시 확인된 직후뿐이다.
- perf 수정 후보가 나와도 그것이 정책 정합성 수정인지, benchmark bug 수정인지, 숫자 부스팅용 편법인지 먼저 구분해야 한다.
- 편법이면 수정하지 않고 바인딩 라이브러리 비효율 제거 쪽으로 다시 돌아간다.

## 6. 성능 비교 기준

반드시 아래를 지킨다.

- 비교 범위는 `single`, `multi`, `multi callback` 전체다.
- `multi callback` 비교에는 최소 `STREAM`, `SPOT` 패턴이 포함되어야 한다.
- 비교 기준은 `core/perf/baseline` report 의 `RESULT,current,...,throughput,...` 값이다.
- 비교 기준 파일은 이번 실행에서 wrapper가 선택해 출력한 `BINDINGS_PERF_BASELINE_FILE` 이다.
- binding 측정은 같은 pattern/transport/size 의미를 가져야 한다.
- primary 판정 지표는 throughput ratio 이다.
- latency는 진단 보조 지표로만 사용한다.
- report가 여러 개면 가장 최근의 comparable report를 기준으로 삼되, 회귀가 보이면 더 넓은 full run으로 재확인한다.
- 측정 중에는 다른 perf run, 다른 언어 bench, 백그라운드 benchmark, 병렬 `ctest`/`dotnet test`/`gradle test`/`cargo test` 등을 겹치게 실행하지 않는다.
- comparable 의미가 맞지 않으면 perf 정합성을 먼저 고치고, 의미가 이미 맞으면 라이브러리를 고친다.

## 7. 언어별 최소 실행 규칙

각 언어 iteration 에서 최소한 아래를 수행한다.

- 해당 언어 perf runner 존재/실행 가능 여부 사전검사
- 관련 빌드 명령
- 관련 테스트 명령
- `single` 부분 perf probe
- `multi` 부분 perf probe
- `multi callback` 부분 perf probe
- 필요 시 전체 comparable perf 재측정

필수 최종 비교 surface:

- `single`
- `multi`
- `multi callback`
- `multi callback` 필수 패턴: `STREAM`, `SPOT`

bug 처리 규칙:

- 바인딩 라이브러리 버그: 해당 `bindings/<lang>/` 코드 직접 수정, 관련 검증 후 perf 개선 계속 진행
- core 라이브러리 버그: `/home/hep7/project/kairos/zlink/core/doc/bug/` 디렉터리를 만들고
  `YYYYMMDD_<lang>_<pattern>_<short-title>.md` 형식으로 버그레포트 작성 후 대기
- core bug를 perf helper 수정이나 binding 우회 코드로 숨기면 안 됨

예시 surface:

- `bindings/<lang>/perf/run_benchmarks.sh`
- `bindings/<lang>/perf/single/run_benchmarks.sh`
- `bindings/<lang>/perf/run_benchmarks_multi.sh`
- 해당 언어 테스트/빌드 surface

## 8. 종료 조건

선택된 모든 언어에 대해 아래가 만족되면 종료한다.

- `single`, `multi`, `multi callback` comparable perf 주요 조합이 언어별 목표 비율 이상이다.
- `multi callback` 에서는 최소 `STREAM`, `SPOT` 패턴이 목표 비율 이상이다.
- 남아 있는 미달 항목이 측정 편차가 아니라 구조적 한계인지 확인됐다.
- 더 진행하려면 core bug report 또는 사용자 정책 결정이 필요한 상태다.
- build/test 가 현재 작업본에서 통과한다.

## 9. Codex 종료 메시지 계약

각 iteration 마지막에는 아래 셋 중 하나만 정확히 출력한다.

- 모든 선택 언어가 현재 목표를 충족했고 더 남은 적용 항목이 없으면:
  `미적용 사항이 없습니다.`
- 사용자 정책 결정이나 명시 입력이 없으면 위험한 상태면:
  `사용자 입력 필요: <짧은 사유>`
- 그 외 아직 남은 적용 항목이 있으면:
  `계속 진행 필요`

## 10. 로그 및 보고 규칙

- 측정 report 경로를 항상 남긴다.
- baseline 대비 ratio는 핵심 미달 항목 위주로 요약한다.
- 변경 파일은 high-signal 만 보고한다.
- bug report가 필요하면 재현 조건, 기대값, 실제값, baseline 근거를 같이 적는다.

## 11. 작업 레지스터

이 섹션은 iteration 중 계속 갱신한다.

### 11.1 현재 실행 컨텍스트

- baseline dir: `BINDINGS_PERF_BASELINE_DIR` 또는 기본값
- selected languages: `BINDINGS_PERF_LANGUAGES` 또는 전체 언어
- target ratios:
  - `cpp`: `BINDINGS_PERF_TARGET_CPP` 또는 `0.95`
  - `dotnet`: `BINDINGS_PERF_TARGET_DOTNET` 또는 `0.90`
  - `go`: `BINDINGS_PERF_TARGET_GO` 또는 `0.85`
  - `java`: `BINDINGS_PERF_TARGET_JAVA` 또는 `0.90`
  - `node`: `BINDINGS_PERF_TARGET_NODE` 또는 `0.75`
  - `python`: `BINDINGS_PERF_TARGET_PYTHON` 또는 `0.75`
  - `rust`: `BINDINGS_PERF_TARGET_RUST` 또는 `0.95`

### 11.2 언어별 상태

상태 해석 규칙:

- `pending`: 아직 이번 실행에서 baseline 재확인을 하지 않음
- `in_progress`: 이번 실행에서 측정/분석/수정 중
- `completed`: 이번 실행에서 다시 측정했고 현재 목표 충족을 확인함
- `blocked`: 사용자 정책 결정, 환경 문제, core bug report 대기 등으로 진행 중단

권장 기록 형식:

- `<lang>: pending`
- `<lang>: in_progress (<surface/status 요약>)`
- `<lang>: completed (single ok, multi ok, callback stream ok, callback spot ok)`
- `<lang>: blocked (<짧은 사유>)`

- `cpp`: pending
- `dotnet`: pending
- `go`: pending
- `java`: pending
- `node`: pending
- `python`: pending
- `rust`: pending

### 11.3 반복 체크리스트

- [ ] baseline comparable surface 확인
- [ ] perf 의미 정합성 점검
- [ ] binding hot path 병목 식별
- [ ] build/test 검증
- [ ] active perf 측정과 겹치는 다른 benchmark/test/build job 없음
- [ ] 부분 perf probe 확인
- [ ] full comparable rerun 확인
- [ ] 목표 ratio 미달 항목 갱신
- [ ] 회귀 실험 원복 여부 확인

## 12. 실행 시작 시 첫 행동

루프가 시작되면 먼저 아래를 수행한다.

1. 선택 언어 목록과 target ratio를 요약한다.
2. 각 언어의 perf runner 존재 여부를 확인한다.
3. 작업 레지스터에 과거 `completed` 표시가 있어도 모두 `이번 실행 기준 pending` 으로 다시 취급한다.
4. 가장 최근 report와 baseline을 비교해 현재 미달 항목을 표로 만든다.
5. `single`, `multi`, `multi callback(stream, spot)` 중 가장 큰 미달 항목부터 하나씩 줄인다.
