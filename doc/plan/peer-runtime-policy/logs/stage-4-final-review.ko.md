# 단계 4 통합 리뷰 로그

- 기록 시각: 2026-04-24T19:57:12+09:00
- 기준 commit: 2a432fa9b03d5fdbb4e42e896f2a6a32b96bc294
- 읽은 초안 section: `peer-disconnect-rid`, `peer-weight`, `auto-hwm`의 구현 체크리스트, 테스트 기준, 문서 반영 목록, 단계 4/5 종료 조건

## 리뷰 요약
- 세 draft의 필수 구현 항목과 정식 문서 반영 목록을 다시 대조했다.
- `core/include/zlink.h` 공개 계약, binding surface, `doc/spec`, `doc/guide`, `doc/internals`, `doc/site/docs`의 의미를 다시 맞췄다.
- 입력 초안으로 남겨둔 untracked draft 3종은 사용자 지시대로 유지했다.

## 필수 검색 결과
- `rg -n "zlink_set_admission_state|zlink_get_admission_state|ZLINK_ADMISSION_|PEER_ADMISSION" core bindings doc/spec doc/guide doc/internals doc/site/docs doc/spec/bindings`
  - 공개 surface 잔여물 없음
  - draft/plan 문서와 historical note 밖의 공개 binding 문서 잔여 설명은 아래 문서 수정으로 제거
- `rg -n "ZLINK_DEALER_OPT_ROUTING_POLICY" core bindings doc/spec doc/guide doc/internals doc/site/docs doc/spec/bindings`
  - draft/plan 문서 외 잔여물 없음
- `rg -n "ZLINK_ROUTER_OPT_HANDOVER" doc/spec doc/guide doc/internals doc/site/docs doc/spec/bindings core/tests bindings/cpp/include/zlink/types.hpp`
  - 남은 항목은 모두 의도된 호환 surface 또는 호환성 설명
  - 유지 위치:
    - `bindings/cpp/include/zlink/types.hpp`
    - `core/tests/integration/test_router_handover.cpp`
    - router/option 관련 spec/guide/internals/site 문서
- `rg -n -f /tmp/zlink-doc-forbidden-terms.txt doc/spec doc/guide doc/internals doc/site/docs doc/spec/bindings`
  - 결과 없음

## 단계 4 수정 사항
- stale public binding 문서 설명 제거
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/node/README.md`
  - `doc/spec/bindings/rust/README.md`
  - `doc/site/docs/api/bindings.md`
- 수정 내용:
  - service-layer entry field 설명을 `admissionState` -> `weight`로 정정
  - Node/Rust binding spec의 admission-state accessor 설명 제거
  - weight는 `RouterSocket`, `DealerSocket`, `SpotNode`, `Spot` typed option facade에만 노출된다는 설명으로 교체

## 문서 역할 검토
- `doc/spec/`: 공개 계약 설명만 유지됨
- `doc/guide/`: 사용자 사용법 중심, 내부 구현 설명 과다 유입 없음
- `doc/internals/`: 유지보수자 구조 설명 중심, 사용자용 사용법 과다 유입 없음
- `doc/site/docs/`: 정식 문서와 같은 의미로 유지됨

## 변경 범위 검토
- `git diff --name-only`로 계획 범위 파일과 로그/문서만 확인했다.
- `framework/...` 아래 기존 사용자 변경은 건드리지 않았고 stage 커밋 대상에서도 제외할 예정이다.
- untracked draft 3종(`doc/draft/*.ko.md`)은 유지했다.

## 남은 구조 문제
- 없음
