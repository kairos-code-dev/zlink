# Node sample wire parity follow-up

이 기록은 E2E process 실행 없이 확인할 수 있는 sample wire·정적 contract 변경을 기록한다.
E2E client/server process 결과는 이 log의 완료 근거가 아니다.

## NS-IMP-001

공통 TicTacToe·SupportChat 계약은 response가 없는 send message에 `Msg` 접미어를 사용한다.
Node sample은 각각 `LeaveGameReq`, `SetTypingReq`를 사용하고 있어 message kind와 source
의미가 어긋났다.

다음 경로를 공통 계약으로 정렬했다.

- TicTacToe: `LeaveGameMsg`, `PacketNames.leaveGameMsg`
- SupportChat: `SetTypingMsg`, `PacketNames.setTypingMsg`
- client, session dispatch와 actor handler의 packet name·type·log 문구

호환 alias나 request/reply wrapper는 추가하지 않았다. 두 message는 기존과 같이 response 없는
send로 유지한다.

## 검증

```text
node --test --test-force-exit \
  test/contract/sample-supportchat-message-semantics-gate.test.js \
  test/contract/sample-tictactoe-internal-join-contract-gate.test.js
  2/2 PASS
npm run typecheck
  PASS
```

실제 sample process와 browser runner는 실행하지 않았다. 해당 evidence는 E2E 제외 범위의
후속 조건이다.

## NS-IMP-002

DeliveryDispatch의 공통 계약은 application message에서 session route와 Actor 위치를 숨기고,
제안 시도의 `attempt`를 Dispatch가 소유하도록 정의한다. Node sample은 이전에
`BindCourierReq/Res`와 `BindCourierSessionRes.sessionRoute`를 노출했고, client-facing
`OfferDeliveryNotify`와 `CourierDecisionMsg`에 `attempt`를 넣었으며 상태 timestamp를 ISO
문자열로 전송했다.

다음 경로를 공통 계약으로 정렬했다.

- session binding response는 `courierId`만 반환하고 session route와 별도 bind request를 제거
- `OfferDeliveryNotify`와 `CourierDecisionMsg`에서는 `attempt`를 제거
- `CourierActor`가 offer별 attempt를 보관해 `OfferDeliveryResultMsg`에만 복원
- 상태 message의 timestamp를 `occurredAtUnixMs: number`로 통일
- Courier session은 bound Actor context를 통해 relay하고 application route를 전달하지 않음

호환 alias나 route wrapper는 추가하지 않았다. stale decision 검사는 Dispatch의 기존
`deliveryId`·`courierId`·`attempt` 비교를 그대로 사용한다.

## NS-IMP-002 검증

```text
node --test --test-force-exit --test-name-pattern='DeliveryDispatch TypeScript sample uses framework channel topology|DeliveryDispatch uses public Actor APIs' \
  test/contract/sample-regression.test.js test/contract/sample-deliverydispatch-spot-handle-gate.test.js
  2/2 PASS
npm run typecheck
  PASS
```

실제 DeliveryDispatch process와 browser runner는 실행하지 않았다. 해당 evidence는 E2E 제외
범위의 후속 조건이다.
