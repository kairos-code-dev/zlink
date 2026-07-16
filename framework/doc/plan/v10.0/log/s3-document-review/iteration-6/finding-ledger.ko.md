# S3 iteration 6 finding ledger

## 1. 집계

| 항목 | 값 |
|---|---:|
| Codex raw finding | 18 |
| Claude Sonnet raw finding | 4 |
| reviewer 사이 중복 | 1 |
| 고유 finding | 21 |
| 수정 과정에서 확인한 파생 불일치 | 1 |
| open | 0 |

모든 finding은 현재 10.0.0 정식 framework 계약을 기준으로 다시 확인했다. 공통 E2E·sample은 누락을
찾는 입력으로만 사용했으며, 그 문서만을 근거로 새 public API를 추가하지 않았다.

## 2. Finding과 closure

| ID | Reviewer | 문제 | Owner·red gate | 상태 | Closure evidence |
|---|---|---|---|---|---|
| I6-CX-01 | Codex | C++ `spot_handle_t` 선언·resolver 누락 | C++ exact declaration inventory | resolved | C++ §15.13에 opaque handle, MeshName accessor와 resolver 추가 |
| I6-CX-02 | Codex | C++ Actor API가 MeshName을 잃음 | C++ actor exact signature | resolved | actor client·directory에 `mesh_name` 추가 |
| I6-CX-03 | Codex | Java Actor·Spot API와 handle이 MeshName을 잃음 | Java exact signature | resolved | client·manager·directory·resolver·handle에 MeshName 고정 |
| I6-CX-04 | Codex | Kotlin 확장이 MeshName 없는 Java 표면을 투영 | Kotlin exact projection | resolved | Actor directory와 Spot resolver 확장을 MeshName-aware로 변경 |
| I6-CX-05 | Codex | Node Actor·Spot API와 handle이 MeshName을 잃음 | TypeScript declaration | resolved | client·manager·directory·resolver·context·handle에 `meshName` 고정 |
| I6-CX-06 | Codex | C++ Actor handler가 mutable Spot과 Actor를 함께 받음 | C++ handler declaration | resolved | Actor-only handler와 immutable membership callback으로 분리 |
| I6-CX-07 | Codex | Java Actor handler·lifecycle의 mutable owner 혼합 | Java handler declaration | resolved | Actor-only handler와 `ZLinkActorMembership` snapshot으로 변경 |
| I6-CX-08 | Codex | Kotlin handler·lifecycle의 mutable owner 혼합 | Kotlin projection | resolved | Actor-only handler와 Java immutable membership 투영 |
| I6-CX-09 | Codex | Node handler·lifecycle의 mutable owner 혼합 | TypeScript declaration | resolved | Actor-only handler와 immutable membership·join request 추가 |
| I6-CX-10 | Codex | Config 10이 `OnJoinedActor`의 mutable Actor 수신을 요구 | Config 10 + server/23 | resolved | lifecycle은 immutable snapshot, state 복원은 Actor query evidence로 변경 |
| I6-CX-11 | Codex | ZoneWorld가 Actor·Spot을 동시에 변경하고 Actor instance를 보관 | ZoneWorld public scenario | resolved | Spot direct update와 immutable `ActorRef` 보관, Actor direct push로 분리 |
| I6-CX-12 | Codex | Bingo Spot이 Actor public method를 직접 호출 | Bingo public scenario | resolved | typed Actor send 뒤 Actor handler가 bound session push 수행 |
| I6-CX-13 | Codex | Bingo의 .NET drain API가 exact 계약에 없음 | common drain owner + 5-language exact | resolved | server/54와 framework API에 MeshNode policy를 고정하고 다섯 언어 exact와 예제를 투영 |
| I6-CX-14 | Codex | 전역 재전송 금지와 Spot stale-route 1회 refresh 모순 | interaction model + server/24 | resolved | handler 미실행을 확인할 수 있는 Spot stale-target 단일 refresh만 명시적 예외로 고정 |
| I6-CX-15 | Codex | Node exception이 숫자 오류 값을 보존하지 않음 | Node exact error object | resolved | `ZLinkFrameworkException.code` 추가 |
| I6-CX-16 | Codex | Connector 정식 wire 계약에 구형 peer decoder 정책이 있음 | Connector §4·§5 | resolved | Response·Error의 nonzero `name_len`을 decode error로 고정하고 호환 문장 제거 |
| I6-CX-17 | Codex | Connector 정식 spec이 비계약 gap 문서를 역참조 | Connector §10 | resolved | gap 링크와 구현·교체 계획 문단 제거 |
| I6-CX-18 | Codex | common README 지원 언어 집합 불일치 | common index | resolved | 현재 exact 대상인 .NET·C++·Java·Kotlin·Node.js만 기록 |
| I6-CL-01 | Claude | Config 11이 존재하지 않는 transfer pending metric을 요구 | Config 11 + runtime metrics | resolved | 정식 `zlink.mesh_node.requests.inflight{surface=actor}` evidence로 교체 |
| I6-CL-02 | Claude | Connector가 비계약 gap 문서를 역참조 | I6-CX-17 | duplicate | I6-CX-17 closure로 함께 해결 |
| I6-CL-03 | Claude | .NET SpotManager에 RID 자동 생성과 GetOrCreate 계열이 없음 | .NET Spot exact | resolved | RID 없는 `CreateAsync`와 RID-aware `GetOrCreateAsync` 계열 추가 |
| I6-CL-04 | Claude | Config 2 SM-D4의 계약 근거 절이 틀림 | Config 2 + server/31 | resolved | bound-session 의미를 소유하는 server/31 §6으로 교체 |
| I6-D-01 | closure audit | 네 언어에 이전 Spot 단위 drain type이 남음 | common drain + 5-language exact | resolved | `ZLinkMeshNodeDrainPolicy` 계열로 통일하고 old type을 forbidden inventory에 추가 |

## 3. Closure gate

- Framework contract verifier: `FRAMEWORK DOC CONTRACTS CLEAN`; exact 24개, formal 48개, 공개 선언
  1,156개, source member 263개 exact-once, feature map 55개, scenario row 950개.
- Finding regression grep: old Spot drain type, mutable Spot+Actor handler, mutable Actor lifecycle, Connector gap
  참조·legacy decoder, 존재하지 않는 pending metric과 잘못된 Config 2 근거가 모두 no-hit다.
- Scope structure: 177개, link 773개, table block 331개, fence 문서 81개, 오류 0.
- E2E·sample·runner·guide inventory: 55·32·4·96·81 전부 존재한다.
- 문서 범위 `git diff --check`, plan 참조 검사와 JSON fixture parse가 통과했다.

수정본은 iteration 7에서 새 aggregate hash로 동결했으며 두 reviewer가 전체 177개를 다시 검토한다.
