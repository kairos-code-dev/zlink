# `[06]` `core` POSD 리팩토링 문서 리뷰 로그

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [05 Phase 3 Engine/Transport/Service](05-core-system-phase3-engine-transport-service-plan.ko.md) |

## Review 4 — 2026-03-16

POSD 관점 심층 리뷰:

- deep module의 인터페이스 축소 정량 목표가 빠져 있었다.
- define errors out of existence 원칙이 적용되지 않았다.
- different layer, different abstraction 검증이 없었다.
- information leakage 원인 분석(설계 시점 오류 vs 점진적 팽창)이 부족했다.
- Phase 1→2 전환 시 temporal decomposition 리스크가 있었다.
- "socket runtime" 용어가 Phase 1과 Phase 2에서 다른 범위로 사용되었다.
- Phase 3 문서에서 3개 phase를 묶은 구조적 근거가 없었다.
- complexity is incremental 방어 원칙이 빠져 있었다.
- general-purpose vs special-purpose 판단 기준이 없었다.

반영:

- 상위 계획에 information leakage 원인 분석, 계층별 추상화 차이 검증,
  인터페이스 축소 기준, define errors out of existence,
  complexity is incremental 방어 원칙,
  general-purpose 판단 기준 추가
- Phase 1에 define errors out of existence 원칙,
  socket runtime 용어 구분, Phase 2 forward reference 추가
- Phase 2에 용어 구분 노트 추가
- Phase 3에 bundling 근거 및 facade 추상화 차이 명시 추가

## Review 5 — 2026-03-16

실제 코드 대조 및 실행 문서 강화 리뷰:

발견:

- [05] public API 현황 표가 실제 코드와 어긋남 (set_pool, add_route 등 존재하지 않는 메서드 기재)
- [01] baseline commit 규칙이 "가능하면"으로 되어 있어 single/multi 다른 commit 허용 여지
- [03] inventory 표에 목표 close 주체 칼럼이 없어 현황/목표 구분 불가
- [02] ownership 표에 복수 주체(또는, /, +) 표현이 남아 있어 단일 owner 원칙 위반
- [04] socket runtime 하위 계약별 information hiding 계약이 없음
- [05] seam/contract freeze 순서가 없고 완료 조건이 정성적
- [05] service API 처리 매트릭스(keep/hide/replace/deprecate + ABI impact)가 없음

반영:

- [05] public surface 표를 실제 코드 기반으로 전면 재작성 (gateway.hpp, spot_node.hpp, discovery.hpp 근거)
- [05] service API 처리 매트릭스 추가 (keep/hide/replace/deprecate + ABI impact: none/wrapper-only/break)
- [05] contract freeze 순서 (engine facade → transport adapter → service surface) 및 step별 선행조건/금지영역/정량 완료 조건 추가
- [01] baseline commit을 single/multi 공통 단일 commit으로 강화, tree 상태(clean) 및 실행 옵션 필수화
- [00] perf gate 절에 동일 baseline commit 규칙 복사
- [03] gateway/spot inventory 표에 목표 close 주체 칼럼 추가 + "현재 상태는 과도기, 목표 상태가 구조 계약" 고정 문장
- [02] ownership 표를 authoritative close owner / shutdown initiator / executor 3칼럼으로 재작성, 복수 주체 금지 규칙 및 역할 정의 추가
- [04] 하위 계약별 information hiding 표 (호출자/숨기는 것/모르는 것) 추가, 정량 완료 조건 추가

## Review 6 — 2026-03-16

thread-safe 규약 문서 연결:

발견:

- POSD 리팩토링 문서 세트에 thread-safe 계약에 대한 참조가 없었다.
- 구조 변경이 현재 thread-safe 보장 수준을 깨뜨릴 위험이 명시되지 않았다.

반영:

- [00] 관련 문서 헤더에 `thread-safe-socket-plan.ko.md` 추가 (현재 구현 수준 유지 전제 명시)
- [00] §2 실패 조건에 "thread-safe 계약 수준 후퇴" 항목 추가, 3계층 계약 요약 및 규약 문서 링크 포함
- [02] nav 표에 thread-safe 규약 링크 추가 — lifecycle strict 계층 참조
- [04] nav 표에 thread-safe 규약 링크 추가 — hot path + control path 계층 참조
- [05] nav 표에 thread-safe 규약 링크 추가 — 3계층 전체 참조

## Review 7 — 2026-03-16

POSD 준수 및 문서 간 정합성 리뷰:

발견:

- [05] ABI impact 칼럼이 C API만 기준인데 완료 조건은 C++ public header 제거를 포함.
  hide 항목의 C++ source-level break가 명시되지 않아 compatibility 기준이 섞여 있었음.
- [02] ownership 표의 executor=reaper와 [03] inventory의 socket_base_t=final destroy owner가
  같은 finalization 체인의 다른 단계를 가리키는데 용어가 정렬되지 않았음.
- [00] dead code 제거 기준이 `grep / IDE reference`로 되어 있어
  실제 워크플로우(`rg`, 빌드 시스템 확인)와 맞지 않았음.

반영:

- [05] ABI impact 정의에 C API 전용 기준 명시, C++ source compatibility 별도 절 추가
  (hide 항목의 C++ 호환성 처리 방향: internal header 이동, migration guide 포함)
- [05] §7.3 및 §10 완료 조건에 "C++ public header에서 제거" 명시 + migration guide 요구 추가
- [02] 역할 정의에 finalization 체인 해석 추가
  (socket_base_t = socket-level cleanup owner, reaper = 최종 memory free executor)
- [03] §4.1.1 목표 해석을 [02]와 동일 체인 용어로 정렬, 교차 참조 문장 추가
- [00] §6.4 제거 기준 도구를 `rg` / 빌드 시스템(`CMakeLists.txt`) / IDE reference로 수정

## Review 8 — 2026-03-16

구현 착수 전 미결 사항 리뷰:

발견:

- [04] socket runtime 하위 계약을 단일 mega-class로 합칠 위험이 있었음.
  구현 제약이 명시되지 않아 `socket_base_t` 대신 큰 `socket_runtime_t`가 생길 수 있었음.
- [03] discovery의 ownership/resource inventory가 빠져 있었음.
  gateway/spot만 있고 discovery는 Phase 3 surface 정리만 되어 있어
  lifecycle 분석 없이 API를 바꾸려는 상태였음.
- [05] discovery `set_socket_option(socket_role, ...)` replace 방침이
  구체적이지 않아 구현 시 C API 방침이 흔들릴 수 있었음.

반영:

- [04] §4.4 추가: socket runtime mega-class 방지 규칙.
  하위 계약은 독립 private component(composition)로 구현,
  component 간 직접 참조 금지, socket runtime method 수 < 이동 전 method 수.
- [03] §7 추가: discovery ownership inventory.
  sub socket, bootstrap/report/control dealers, control task, monitor, observers,
  service state 리소스 표 + 현재 구조 해석 + 목표 해석.
  리팩토링 우선순위에 discovery 항목 추가, 완료 조건에 discovery 포함.
- [05] §6.3 매트릭스 하단에 discovery `set_socket_option` replace 방침 추가:
  기존 C API 시그니처 유지 + 내부 role→socket 매핑만 변경 + 새 API 추가 불필요.

## Review 9 — 2026-03-16

discovery inventory 심화 리뷰:

발견:

- [03] §7 discovery inventory가 gateway/spot 대비 얕았음.
  destroy 경로 흐름도, 동적 dealer lifecycle 패턴, observer/summary ordering 제약이 없었음.
- destroy 흐름의 5단계 분해(API guard → monitor/task 정리 → snapshot → socket close 순회 → drain+통지)가
  명시되지 않아 Phase 4 총 시간(3N+1초)이나 snapshot-close 분리 의도가 보이지 않았음.
- bootstrap dealer(ephemeral)와 report/control dealer(persistent per endpoint)의
  lifecycle 패턴 차이가 정리되지 않아 endpoint registry 하위 계약 연결이 불분명했음.
- observer 통지가 socket close + wait_drained 이후인데,
  summary store clear는 snapshot 단계(socket close 이전)에서 수행되어
  ordering 계약 의도가 불분명했음.

반영:

- [03] §7.3에 observer/summary ordering 제약 항목 추가
  (summary clear가 socket close 이전, observer 통지가 이후 — 의도적 계약인지 판정 필요)
- [03] §7.3.1 추가: destroy 경로 5단계 흐름도 + Phase 4 시간 비례 분석 + snapshot-close 분리 해석
- [03] §7.4 추가: 동적 dealer lifecycle 패턴 (bootstrap=ephemeral vs report/control=persistent)
  lifecycle 표 + 삭제 trigger 이중화 ownership 냄새 + Phase 2 endpoint registry 연결점 명시
- [03] §7.4 목표 해석을 §7.5로 이동

## Review 10 — 2026-03-16

호환성 제약 제거:

전제 변경:

- 이번 리팩토링은 기존 API 호환성을 유지하지 않는다.
- C API, C++ facade 모두 리팩토링 대상이다.

발견:

- [00] 우선순위 3이 "API/ABI 충격 최소화"로 되어 있어 불필요한 보수성을 강제했음.
- [00] §3.4 information leakage 결론이 "기존 public API를 크게 바꾸지 않고"로 되어 있었음.
- [05] 전반에 C API 호환성 기준(시그니처 유지, ABI impact, wrapper-only, break = 0)이 산재했음.
- [05] §6.3 매트릭스가 ABI impact 중심이라 "가장 단순한 API가 무엇인가"를 묻지 않았음.
- [05] discovery set_socket_option이 "기존 시그니처 유지 + role 호환"으로 제약되어 있었음.

반영:

- [00] 우선순위 3을 "설명 가능한 ownership / deep module"로 교체
- [00] §3.4 결론을 "API도 호환성 없이 재설계"로 수정
- [00] §7.1 API/Binding 목표에서 binding 호환 언어 제거
- [05] §6.1 "실제 public compatibility 기준은 C API" → "기존 API 호환성을 유지하지 않는다"
- [05] §6.2 C API/C++ 구분 주의사항 제거, 통합 inventory로 단순화
- [05] §6.3 매트릭스 재구성: ABI impact 칼럼 제거, 처리를 keep/replace/remove/internalize로 변경
- [05] discovery set_socket_option: replace(wrapper-only) → remove(semantic API로 재설계)
- [05] §6.3 목표를 호환성 없는 재설계 기준으로 수정
- [05] §7.3 "service surface reduction" → "service surface redesign", C API break 금지 제거
- [05] §10 정량 기준에서 "C API 시그니처 break = 0" 제거

## Review 11 — 2026-03-16

최종 소스 구조 문서 추가:

발견:

- 상위/phase 문서는 책임 경계는 충분히 설명하지만,
  리팩토링 완료 후 `core/src`를 어떤 트리로 읽어야 하는지는 한곳에 정리돼 있지 않았음.
- 디렉터리 이동보다 책임 경계가 우선이라는 원칙은 있었지만,
  최종 상태의 코드 탐색 기준이 없어 구현 후반 rename/move 판단이 흔들릴 수 있었음.

반영:

- [07] `07-core-system-target-source-layout.ko.md` 추가
- 최종 `core/src` 목표 트리 초안
- 디렉터리별 책임 / 금지 책임 / 해석 추가
- 파일 이동 원칙과 Phase 7 정리 기준 명시
- `README` 문서 맵에 [07] 추가

## 최종 상태

현재 최종 방침:

- 이번 리팩토링은 기존 API 호환성을 유지하지 않는다.
- C API, C++ facade 모두 semantic purity 기준으로 재설계한다.
- internal concept을 노출하는 API는 keep하지 않고 replace/remove/internalize 중 하나로 정리한다.

현재 문서 세트는 아래를 충족한다.

- 상위 방향 문서
- baseline 문서
- ownership 원칙 문서
- ownership inventory 문서
- socket runtime 분리 문서
- engine/transport/service 통합 재구성 문서
- 리뷰 로그
