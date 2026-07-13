# Java AutomaticTurnDispatch E2E feature map

기준 문서는 `framework/doc/framework/common/e2e/config-8-automatic-turn-dispatch.ko.md`다.

| Track | 시나리오 | Java 검증 경로 |
|---|---|---|
| A | ATD-A1~A4 | Spot request와 worker call의 단일 `CompletionStage` terminator, 다른 Spot 작업 진행, continuation context |
| B | ATD-B1~B3 | 서로 다른 actor mailbox 진행, 같은 actor 재진입 금지, Entry Spot의 actor join 대기 |
| C | ATD-C1~C3 | 서로 다른 timer 진행, 같은 timer 재진입 금지, actor와 timer mailbox 분리 |
| D | ATD-D1~D4 | local/remote Spot, route bridge, bound session actor relay |
| E | ATD-E1~E5 | timeout, Java stage cancellation, shutdown recovery, 허용 표면 정적 검사, 공통 marker report |

Java에는 framework `CancellationToken`을 노출하지 않는다. ATD-E2는 request가 반환한 Java
`CompletionStage`를 취소했을 때 continuation과 mailbox가 정리되는지 검증한다. ATD-E4는 server
fixture에 `.await(`, handler blocking `await`, HTTP scenario trigger가 없는지 runner가 확인한다.

완료 판정은 build 성공만으로 하지 않는다. full runner가 19개 ID를 report에 기록하고 실제
connector, route, Spot, actor, timer, shutdown 경로를 모두 통과해야 한다.
