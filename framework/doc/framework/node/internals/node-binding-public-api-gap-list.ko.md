# Node Binding Public API Gap List

> 이 문서는 Node.js/NestJS framework 가 `@zlink-systems/zlink` 공개 API 위에서만
> 동작하기 위해 필요한 바인딩 기능을 점검한 결과다. framework 는
> `bindings/node` 의 internal, native, generated helper 경로를 직접 호출하지
> 않는다. 필요한 기능이 빠져 있으면 framework 에 우회 코드를 넣지 않고
> 바인딩의 공개 API를 먼저 보강한다.

## 현재 판정

2026-06-02 기준 P1.5에서 확인한 공개 API gap은 0개다.

| 영역 | framework 필요 기능 | 공개 API 근거 | 판정 |
|------|---------------------|---------------|------|
| context | framework runtime context 생성과 종료 | `createContext`, `Context.close`, `Context.shutdown` | gap 없음 |
| channel socket | dealer/router/pub/sub 생성, discovery attach, send ready, monitor open | `createDealerSocket`, `createRouterSocket`, `createPubSocket`, `createSubSocket`, socket public methods | gap 없음 |
| monitoring | socket monitor 생성, event callback, status/recv | `Socket.monitorOpen`, `MonitorSocket.onEvent`, `recv`, `status` | gap 없음 |

## 2. 회귀 테스트

P2~P8 구현 중 위 표의 기능을 더 깊게 사용하다가 공개 API가 부족하면 다음 순서로
처리한다.

1. Node framework 의 `runtime/backend/contracts` 와 adapter, contract test 경로를 확인한다.
2. `bindings/node` 공개 계약(`dist/index.d.ts`, `src/zlink/contracts`)에 필요한
   기능이 있는지 확인한다.
3. 없으면 `bindings/node` 공개 API와 테스트를 먼저 추가한다.
4. framework adapter 는 공개 API만 호출한다.

이 문서의 판정은 아래 테스트로 고정한다.

| 테스트 | 고정하는 내용 |
|--------|---------------|
| `node-binding-parity.test.js` | 필요한 공개 API가 존재하고 기본 wrapper smoke가 통과한다 |
| `backend-public-api-only.test.js` | framework package가 binding internal/native 경로를 import하지 않는다 |
| `native-artifact-freshness.test.js` | 현재 플랫폼에서 로딩되는 native addon이 native source보다 오래되지 않았다 |
