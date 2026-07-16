# Node.js RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

현재 상태: Node.js `RuntimeMonitoring` config는 MON-A1~MON-D1이 구현되어 있다. 이 문서는 `.NET`
`framework/languages/dotnet/e2e/RuntimeMonitoring/feature-map.ko.md`와 공통 문서의 scenario ID를 기준으로
포팅 범위를 고정한다. 내부 helper나 raw-frame 우회로 완료 표시하지 않는다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| MON-A1 | 구현 | trigger transient client가 service channel에 연결/해제하고 service socket monitoring evidence의 `Connected`/`ConnectionReady`와 `Disconnected`/`Closed` marker를 검증했다. 로그: `logs/20260703-220339-17357` |
| MON-A2 | 구현 | `svc-b`를 종료하고 재기동해 `svc-a`의 `TopologyChanged` node 수와 `ServiceSummaryChanged` total 수가 감소한 뒤 복구되는지 검증했다. 로그: `logs/20260715-075251-2259558` |
| MON-A3 | 구현 | service spot monitoring source가 `StatusChanged`, `PeersChanged`, `SubjectsChanged`, `TimerHandlerFailed` evidence marker를 기록하고 failing timer 이름을 함께 남기는지 검증했다. 로그: `logs/20260703-220339-17357` |
| MON-A4 | 구현 | terminal `Drained`와 old row 제거 뒤 같은 rid·다른 endpoint의 replacement readiness를 확인하고, 별도 `SIGKILL` 뒤 owner lease 만료·`Disconnected`·남은 provider 후속 요청을 검증한다. socket weight 0/100 변경은 drain과 분리해 `PeerAdmissionChanged`로 확인한다. 로그: `log/20260716-113646-3682839` |
| MON-A5 | 구현 | malformed raw TCP attempt가 native `Disconnected` event의 handshake failure reason으로 들어올 때 Node monitoring mapper가 public `HandshakeFailed` kind로 노출하고, location runtime/spot status와 `TimerStoppedAfterUnhandledException`도 함께 검증했다. 로그: `logs/20260703-220339-17357` |
| MON-B1 | 구현 | filtered service role이 재시작 뒤에도 socket source를 `ConnectionReady`로 제한하고, service-B evidence에 socket event가 해당 kind로만 남는지 검증했다. 로그: `log/20260716-113646-3682839` |
| MON-B2 | 구현 | duplicate source, non-positive interval, missing spot source, missing socket source validation이 실제 Nest application context 시작 과정에서 명확한 오류로 걸러지는지 검증했다. 로그: `log/20260716-113646-3682839` |
| MON-C1 | 구현 | throwing service role의 monitoring handler 예외가 `monitoring-event-dispatch` stderr marker로 보고되고 이후 같은 trigger request가 성공하는지 검증했다. 로그: `logs/20260703-220339-17357` |
| MON-D1 | 구현 | service-B shutdown/restart 뒤 같은 endpoint request와 restarted evidence marker가 성공하고 location runtime topology continuity evidence를 확인한다. 재시작 process는 다음 lifecycle 시나리오까지 명시적으로 인계한다. 로그: `log/20260716-113646-3682839` |

검증:

- `framework/languages/node/e2e/RuntimeMonitoring/run_e2e.sh`
  - PASS: `logs/20260703-220339-17357`
  - MON-A2 topology change 단독 PASS: `logs/20260715-075251-2259558`
  - MON-D1 단독 PASS: `logs/20260703-220318-16504`
  - `svc-a.evidence.log`에는 malformed raw TCP attempt의 `HandshakeFailed` socket evidence와 spot fixed kind evidence가 함께 남는다.
- 미착수 scenario: 없음

후속 계약 판정:

- 없음. MON-A5는 native disconnect reason이 handshake failure일 때 public `HandshakeFailed` kind로 매핑하도록 고쳤고,
  전체 runner에서 marker를 확인했다.
