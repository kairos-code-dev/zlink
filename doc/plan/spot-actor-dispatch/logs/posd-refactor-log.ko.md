# POSD Refactor Log

- 날짜: 2026-05-04
- 대상: repo 전체
- 수행한 명령: `sed -n '1,220p' doc/principal/software-design-principles.md`
- 발견한 문제: 기능 구현 전이므로 리팩토링 후보 기록 전
- 수정한 파일: 없음
- 남은 위험: 기능 구현 완료 뒤 POSD 루프 필요
- 다음 확인: 기능 구현과 문서-코드 반복 리뷰 이후 전체 스캔

## 2026-05-04 core 구현 후 1차 점검

- 대상: `core/src/api/service_spot_actor_api.cpp`
- 위험 신호:
  - 얕은 public API wrapper가 아니라 하나의 큰 file에 Actor table, join queue, session binding, active route가 함께 들어 있어 정보 은닉 경계가 아직 거칠다.
  - remote control plane이 실제 mesh protocol이 아니라 process-local registry를 직접 조회하므로 이름과 구현 깊이가 일치하지 않는 부분이 있다.
  - timeout 처리와 pending join cleanup이 request timeout scheduler에 흡수되지 않아 시간 관련 예외가 호출자 계약과 완전히 맞지 않는다.
- 적용한 원칙:
  - active route sync 여부는 호출자가 넘기는 flag가 아니라 `SpotNode`/`Discovery` 내부 상태에서 판단하게 해 복잡성을 아래로 내렸다.
  - generic route 공개 API를 제거해 core가 의미를 모르는 key-value 생명주기를 application에 맡기던 얕은 표면을 줄였다.
- 남은 선택지:
  - 대안 A: 현재 file을 유지하고 테스트만 늘린다. 빠르지만 Actor table과 session binding 지식이 계속 커진다.
  - 대안 B: Actor owner state, join queue, session binding, active route publisher를 내부 모듈로 나눈다. 초기 비용은 있지만 정보 은닉이 더 좋아진다.
- 선택: 후속 리팩토링은 대안 B가 맞다. 다만 이번 turn에서는 core build/integration 통과를 먼저 고정했다.

## 2026-05-05 POSD 루프 1차

- 대상:
  - `core/include`
  - `core/src`
  - core tests
  - `bindings`
  - samples
  - scripts
  - docs와 codegen 도구
- 기준 문서: `doc/principal/software-design-principles.md`
- 수행한 스캔:
  - Actor public surface와 정식 spec 대조
  - `core/src/api/service_spot_actor_api.cpp`의 Actor table, join queue,
    session binding, active route, ownership, timeout 경로 검토
  - `core/include`, `core/src`, core tests, bindings, samples, scripts, docs의
    stale Actor/generic route 이름 검색
  - `TODO`, `FIXME`, temporary 표현 검색. 기존 repo의 unrelated TODO와
    temporary backpressure 설명은 이번 Actor dispatch 리팩토링 후보에서 제외
- 후보 1:
  - 위치: `core/src/api/service_spot_actor_api.cpp`
  - 위험 신호: session Actor list 지식과 Actor bound session field reset 규칙이
    bind, unbind, stream cleanup, stale validation, destroy cleanup에 반복됨
  - 위반 원칙: 정보 은닉, change amplification
  - 대안 A: 호출부마다 field를 계속 직접 초기화한다.
  - 대안 B: `clear_actor_bound_session_locked()` helper로 bound session 해제 규칙을
    한 곳에 둔다.
  - 선택: 대안 B
  - 선택 이유: Actor bound session 표현이 바뀌어도 helper만 수정하면 되며, 호출자는
    “해제한다”는 의도만 드러낸다.
  - 호출자 복잡성 변화: 각 API 경로가 `bound_session_node`, `bound_stream`,
    `bound_session_rid`, `last_changed_ms` 조합을 알 필요가 줄었다.
  - 테스트:
    - `cmake --build core/build --target test_spot_actor_dispatch -j"$(nproc)"`
    - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
  - 결과: 통과
- 후보 2:
  - 위치: `core/src/api/service_spot_actor_api.cpp`
  - 위험 신호: joined Spot 해제 규칙이 local leave, ref leave, Spot destroy cleanup에
    반복됨
  - 위반 원칙: 정보 은닉, 오류를 정의로 없애라
  - 대안 A: 각 호출부에서 `joined_spot = NULL`, `last_changed_ms`, active route
    update를 직접 수행한다.
  - 대안 B: `clear_actor_joined_spot_locked()` helper로 join 해제와 active route
    갱신을 묶는다.
  - 선택: 대안 B
  - 선택 이유: joined state와 active route publish timing이 어긋날 가능성을 줄인다.
  - 호출자 복잡성 변화: 호출부는 어떤 Actor가 leave되는지만 결정하고 route 갱신
    절차는 내부 helper가 처리한다.
  - 테스트:
    - `cmake --build core/build -j"$(nproc)"`
    - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
    - `ctest --test-dir core/build --output-on-failure -L integration -j1`
  - 결과: unit 21/21 통과, integration 63/63 통과
- 보류 항목:
  - `service_spot_actor_api.cpp`가 큰 단일 파일인 점은 유지한다.
  - 유지 사유: 현재 Actor table, session Actor list, join queue, active route timing은
    같은 lock과 같은 ownership 규칙을 공유한다. 지금 API 단계별 파일로 나누면
    구현 순서 기준 temporal decomposition이 되고, 같은 Actor table 지식이 여러
    파일에 새어 나간다. public 계약과 ABI를 바꾸지 않고 더 깊은 내부 class로
    옮기는 리팩토링은 core release 뒤 별도 작업으로 다루는 편이 안전하다.

## 2026-05-05 POSD 루프 2차

- 수행한 스캔:
  - `rg -n "bound_session_node = NULL|bound_stream = NULL|memset \(&.*bound_session_rid|joined_spot = NULL" core/src/api/service_spot_actor_api.cpp`
  - `rg -n "zlink_discovery_(bind_route|unbind_route|resolve_route)|zlink_route_kind_t|ZLINK_ROUTE_KIND_INVALID|ZLINK_ROUTE_KEY_MAX|ZLINK_ROUTE_VALUE_MAX|zlink_spot_node_remote_actor_ref|zlink_stream_lookup_actor|zlink_stream_send_actor_part|session_actor_key|Actor HWM|actor HWM|generation == 0.*invalid|invalid.*generation == 0|RemoteActor" doc/spec/core doc/guide doc/internals doc/site/docs --glob '!doc/spec/draft/**' --glob '!doc/plan/**'`
  - Actor API list를 public header와 정식 spec에 다시 대조
- 결과:
  - bound session field reset은 constructor와 helper 내부에만 남았다.
  - joined Spot reset은 helper 내부에만 남았다.
  - 정식 문서와 site docs에서 제거된 공개 API와 stale Actor 설계 이름이 검색되지
    않았다.
  - 새 POSD 리팩토링 후보는 없다.
- 테스트:
  - `ctest --test-dir core/build --output-on-failure -R '^test_helper_more_bad_send$' -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_xpub_nodrop$' -j1`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 결과:
  - 중간 전체 integration 실행에서 `test_helper_more_bad_send` timeout과
    `test_xpub_nodrop` TCP drain timeout이 각각 한 번 발생했지만, 두 테스트 모두
    단독 재실행에서 통과했다.
  - 최종 integration 재실행은 63/63 통과했다.
