# Monitoring — runtime 이벤트 관찰

monitoring 은 runtime 상태 변화를 typed event 로 전달한다. socket은 backend monitor event를
framework event로 바꾸고, location runtime과 Spot은 snapshot 차이를 event로 전달한다.

## 1. event handler

```ts
@zlinkRuntimeEventHandler()
export class LocationRuntimeMonitor implements ZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent> {
  async handle(event: ZLinkLocationRuntimeEvent): Promise<void> {
    console.log(event.sourceName, event.event); // source와 변경 종류를 함께 기록한다.
  }
}
```

## 2. source

| source | 방식 |
|--------|------|
| socket | raw monitor event 를 framework event 로 변환 |
| location runtime | status, topology, service summary snapshot diff |
| Spot | status, peers, subjects snapshot diff |

discovery 는 별도 runtime event가 아니다. 현재 provider 상태는 location runtime query로 본다.

## 3. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/location runtime/Spot **상태 변화**를 본다면, 메시지 흐름 추적은 한 메시지가
**도착했나 / 핸들러로 갔나 / 응답이 나갔나**를 dispatch 길목에서 찍는다. core 와 NestJS 양쪽에서
`configureDispatch()` 체인으로 켠다.

```ts
const builder = zlinkFramework();
builder.configureDispatch()
  // off → errorsOnly(기본) → keyTransitions → verbose → diagnostic
  .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
  .traceLogFile(`${process.env.LOG_DIR ?? 'logs'}/flow-api.log`)  // 미지정이면 console.error 폴백
  .traceLabel('api');                                            // 구조화 필드 label=
```

- 모드 게이팅: `Dropped`·에러는 `ErrorsOnly` 이상, 성공 전이는 `KeyTransitions` 이상. `Off` 면
  트레이서가 이벤트를 만들지 않아(제로-alloc) 운영 성능에 영향이 없다.
- 운영 중 켜고 끄기: host 의 `setMessageFlowMode(...)`(공유 live cell, 재시작 불필요).
- 콜렉터/OTel 연동: `setMessageFlowObserver(ObserverType)`로 구조화 이벤트를 받는다(앱 레이어).
- 정식 계약: [Node 공개 interface](../../spec/server/languages/node/02-handler-interfaces.ko.md), 공통 의미:
  [공통 스펙 메시지 흐름 추적](../../spec/server/52-message-flow-tracing.ko.md).

## 회귀 테스트

socket, location runtime, Spot monitoring source는
`framework/languages/node/test/contract/monitoring-runtime.test.js`에서 확인한다.
