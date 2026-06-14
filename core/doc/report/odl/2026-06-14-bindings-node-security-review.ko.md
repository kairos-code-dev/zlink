# Node 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/node/native/src/addon_core.cc`, `bindings/node/native/src/addon_message_parts.h`, `bindings/node/native/src/addon_tsfn_slots.h`, `bindings/node/dist/index.d.ts`
- 검토 방식: N-API callback slot, message part 소유권, public type 정의를 코드 기준으로 확인했다.
- 상태: 2026-06-14 주의 항목 1건 문서화 완료. Codex 에이전트 리뷰 통과.

## 요약

Node 바인딩은 N-API native addon으로 core C API를 감싼다. 이번 검토에서는 JavaScript callback으로 넘어가는 thread-safe function 슬롯, multipart 메시지 소유권, public type 정의를 확인했다.

메시지 part close와 실패 복구 경로에서는 명백한 누수나 double close를 확인하지 못했다. 다만 callback slot 수가 고정되어 있어 동시 handler 수가 늘어나는 사용에서는 기능 제한으로 나타날 수 있다.

## 확인된 이슈

### NODE-BINDING-001: callback handler slot 수가 고정되어 있다

- 심각도: 낮음
- 상태: 2026-06-14 문서화 완료
- 근거:
  - `bindings/node/native/src/addon_core.cc:22-24`는 stream, send-ready handler, socket monitor handler slot 수를 각각 `8`로 고정한다.
  - `bindings/node/native/src/addon_tsfn_slots.h:7-41`은 고정 배열에서 빈 slot을 찾고 reset한다.
  - `bindings/node/native/src/addon_core.cc:1034-1074`는 send-ready handler attach 시 빈 slot이 없으면 실패한다.
  - `bindings/node/native/src/addon_core.cc:1081-1122`는 monitor handler attach 시 빈 slot이 없으면 실패한다.
  - `bindings/node/native/src/addon_core.cc:2398-2421`은 stream callback slot이 없으면 실패한다.
  - `bindings/node/native/src/addon_core.cc:3219-3221`과 `3748-3755`는 timer handler slot도 `8`개 고정이며 빈 slot이 없으면 실패한다.
- 영향:
  - 보안 취약점보다는 용량 제한이다.
  - 많은 socket이나 monitor에 동시에 handler를 붙이는 애플리케이션에서는 예외가 발생할 수 있다.
  - 고정 배열이라 정상 범위에서는 성능이 예측 가능하지만, 확장성은 제한된다.
- 권장 조치:
  - 공개 문서에 동시 handler 수 제한을 적거나, 동적 slot 관리로 바꾼다.
  - 동적 구조로 바꾸더라도 slot 해제와 N-API finalizer 순서를 회귀 테스트해야 한다.
- 처리 결과:
  - `bindings/node/README.md`와 `bindings/node/README.typedoc.md`에 native thread-safe-function slot이 callback family별로 최대 8개이며, 기존 owner를 닫아 slot을 해제해야 같은 family에 더 붙일 수 있다고 적었다.
  - 이번 조치는 현재 native trampoline 배열 구조를 바꾸지 않고 공개 제한을 문서화하는 방향으로 처리했다.

## 메시지 소유권 확인

- `bindings/node/native/src/addon_message_parts.h:17-34`는 native 메시지를 vector slot으로 move하고, move 실패 시 slot을 닫고 vector에서 제거한다.
- 같은 파일 `36-84`는 receive part 수집 중 실패하면 이미 받은 part를 닫고 vector를 비운다.
- 같은 파일 `86-91`은 multipart close helper를 제공한다.
- `bindings/node/native/src/addon_core.cc:683`, `694`, `706`, `963`, `3242` 주변의 N-API finalizer들은 callback slot 상태를 해제한다.

## 기능·성능 검토

고정 slot 구조는 작은 수의 handler에서는 단순하고 빠르다. 그러나 handler 수가 많아지는 서버에서는 기능 제한이 먼저 드러난다. 메시지 part 처리는 실패 시 close 경로가 있어, 검토한 범위에서는 성능보다 소유권 안정성에 무게를 둔 구조로 보인다.

검증:

- `cd bindings/node && npm run build && npm run typecheck` 통과.
- `cd bindings/node && npm run rebuild-native && npm test` 통과. Node tests, samples 14개, codec tests가 통과했다.
- Codex 에이전트 리뷰에서 "추가 이슈 없음" 판정을 받았다.

## 결론

Node 바인딩에서 확인된 주요 항목은 동시 callback handler 수 제한이다. 2026-06-14에 callback family별 동시 handler 제한을 public 문서에 명시했다. 메시지 part 소유권 경로에서는 추가 수정할 보안 이슈를 확인하지 못했다.
