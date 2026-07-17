# S5 Core 구현 리뷰 manifest — iteration 1 (최종 전체 pass)

## 1. 리뷰 목적

S4에서 구현한 Core 10.0.0 RouteMesh 전환을 frozen spec 기준으로 검증하는 구현
리뷰다. S4 구현 중 개별 checkpoint 리뷰를 열지 않았으므로(ledger §2.1 대체
규칙), 이 iteration이 catch-up과 **최신 snapshot 전체 campaign scope의 마지막
전체 pass**를 겸한다. P4(최종 적대 검토) 우선 축으로 수행하되 I1·I2·I3 세 축을
각각 독립 판정한다.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 1 |
| Acceptance candidate commit | `8206fd44dcd4cdd32e0364d0095631edb7b5118e` |
| Working tree diff | 없음 (커밋 시점 clean) |
| Scope 파일 수 | 625 |
| Scope aggregate SHA-256 | `281c02e49e016ce567cef652628f512d97495ca4af3b7eacc035361bf964e683` |

Scope hash 재계산:

```bash
files=$(git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md | sort -u)
printf '%s\n' "$files" | xargs sha256sum | sha256sum
```

## 3. 읽어야 하는 정식 spec과 계획 문서

- 정식 계약(구현 일치 판단 기준): `core/doc/spec/core/` 전체(52 Markdown),
  특히 `service/01-mesh-node`, `service/02-dispatch`, `service/03-spot`,
  `service/04-actor`, `service/05-stream-session`, `04-errno-map`,
  `06-polling`, `07-monitoring`, `03-errors`
- 실행 진행표: `framework/doc/plan/v10.0/route-mesh-10.0.0-execution-ledger.ko.md` §8
- 검증 matrix: `framework/doc/plan/v10.0/s4-core-contract-test-matrix.ko.md`

리뷰어는 spec을 구현 일치 판단 기준으로만 읽고 spec 자체의 문서 품질은
판정하지 않는다(S3 종결).

## 4. 검토 scope

- source: `core/src/runtime/services/mesh/`, `core/src/api/mesh/`,
  `core/src/api/socket/`(유지 raw 기계), `core/src/api/monitoring/`,
  `core/include/zlink/` 공개 header 폐쇄
- test: `core/tests/integration/test_mesh_*`, `core/tests/contract/`,
  `core/tests/unittest/` 신규·갱신분과 기존 회귀 유지분
- build·package: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`,
  `core/packaging/conan/`, `.github/workflows/{build,core-conan-release}.yml`
- 문서(구현 일치 확인 대상): `core/doc/internals/`, `CHANGELOG.md`

## 5. 제거 API와 금지 구현 검색 문자열

`core/tests/contract/removed-identifiers-10.0.0.json` 전체와 다음 word-boundary
패턴의 scoped no-hit(removal manifest·부재 검증 test 제외):
`\bspot_node\b`, `\bSpotNode\b`, `zlink_spot_node_`, `ZLINK_SPOT_NODE_MODE_`,
`\bmesh_pub\b`, `\bmesh_xsub\b`, `zlink_spot_route_bridge_`, `\bRouteBridge\b`,
`zlink_spot_dispatch_event_handler`, `spot_dispatch_worker`, `\bspot_subject_`,
`zlink_msg_gets`, `_spot_part\b`

## 6. 실행해야 하는 검증 명령과 기존 결과

| 명령 | 기존 결과 (2026-07-17) |
|---|---|
| `cmake --build core/build -j && ctest --test-dir core/build -j` | 100% tests passed, 0 failed out of 84 |
| `./core/build-asan/bin/test_mesh_*` (4 바이너리) | 전부 OK, leak 0 |
| `setarch -R ./core/build-tsan/bin/test_mesh_*` | mesh 신규 코드 race 0 (기존 기계 3계열 기록) |
| `contract_public_surface` (ctest 포함) | PASS: 196 export 정확 일치·제거 0·한영 C block 일치 |
| C ABI smoke (staging install + C11 consumer) | `C ABI SMOKE PASS (zlink 10.0.0)` |

## 7. 직전 finding과 반영

S5 첫 iteration이므로 이전 S5 finding은 없다. S4가 스스로 적발·수정한 결함
(ASAN heap-UAF 2건, TSAN race 2건, errno mapping 6종, monitor event 7종 미방출,
reply-after-STOPPED)은 ledger §8 각 행에 기록되어 있고, 아래 known risk는 이
리뷰에서 반드시 판정한다.

- TSAN 기존 기계 3계열: `part_helper_state` check-then-set, socket 생성 경로
  auto-HWM lock-order-inversion, mailbox ypipe 계열 경고
- `ZLINK_MESH_PEER_MIXED` 병합 분기의 실도달 경로(수동 intent endpoint와
  inbound peer endpoint 불일치)
- peer state `DRAINING`이 상태 값으로는 정의되나 어떤 전이도 이 값을 설정하지
  않음(generation 교체는 즉시 치환 + `PEER_DRAINING` event만 방출)
- shutdown deadline 이후 timeout 없는(무기한) operation의 종료 경로

## 8. Pass·축·시간

- Pass 번호: campaign 최종 pass (P4 우선 축, 전체 scope 처음부터)
- 세 축 판정: I1 계약 구현 일치 / I2 POSD·DDD / I3 정리 완결성 — 각각
  finding 또는 `없음`, evidence, `CLEAN`/`NOT CLEAN`
- 목표 60분, 강제 종료 90분. `PARTIAL`이면 검토한 파일·축·명령·남은 범위 기록

## 9. 리뷰어와 결과 파일

| 항목 | R1 | R2 |
|---|---|---|
| Reviewer | Codex agent | Claude Fable (fallback: Claude Sonnet) |
| 수정 권한 | 없음 | 없음 |
| 결과 파일 | `codex-review.ko.md` | `claude-fable-review.ko.md` |
| 원본 출력 | `codex-raw-output.txt` | `claude-fable-raw-output.txt` |

두 리뷰어는 서로의 결과를 보지 않은 상태에서 같은 snapshot을 검토한다.
blocker·high finding이 없고 세 축이 모두 `CLEAN`이면 결과 마지막 줄에 정확히
다음 한 줄을 남긴다.

```text
CORE REVIEW CLEAN
```
