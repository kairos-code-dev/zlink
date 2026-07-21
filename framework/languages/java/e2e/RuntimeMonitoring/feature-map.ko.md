# Java RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

## 10.0.0 목표 판정

Java E2E는 각 host의 public `ZLinkRouteMeshRuntime` snapshot과 typed event를 검증한다. 기존
`ZLinkMonitoringOptionsCustomizer`·`ZLinkRuntimeEventHandler` source marker는 관련 행의 부분
증거로만 사용한다. Client는 HTTP driver이고 framework operation은 Trigger와 Service role이
실행한다.

| 시나리오 | 상태 | 현재 증거 | 남은 gap |
|---|---|---|---|
| MON-A1 | 구현 | `svc-a` 단독 snapshot과 `svc-b` ready 뒤 snapshot에서 MeshNode·peer·channel·multicast·claim·location·drain 값, sequence 증가와 이전 값의 불변성을 확인했다. | 없음 |
| MON-A2 | 구현 | Runtime observer를 연 상태에서 `svc-b`를 정상 종료·재시작해 peer event, 같은 RID, 새 lifecycle generation, descriptor revision, endpoint, admission·ready·last failure field를 snapshot과 대조했다. | 없음 |
| MON-A3 | 구현 | `svc-b` RouteMesh weight를 0으로 바꿔 local weight와 `svc-a` ready member 수 1을 확인하고 실제 select-one request가 `svc-a`만 선택함을 검증했다. 복원 뒤 ready member 수 2와 `svc-b` 재선택도 확인했다. | 없음 |
| MON-A4 | 10.0.0 전환 대상 | Service admin 종료 뒤 같은 binary·endpoint로 재시작하고 후속 request와 topology down/up을 확인한다. | 정상 replacement와 fresh topology의 `SIGKILL`·lease 만료를 나누고 generation·endpoint·ready member가 최신 snapshot으로 수렴하는지 검증한다. |
| MON-A5 | 구현 | Redis를 정지·복구해 `zlink.runtime.location.store_changed`의 `degraded`·`ready` 전이, location state·last success·last failure, 장애 중 admitted messaging 유지와 복구 뒤 ready topology를 확인했다. | 없음 |
| MON-B1 | 부분 구현 | remote application mailbox와 ROUTER HWM에 압력을 가해 terminal result, `multicast_backpressured` event, submitted·backpressured 누계와 수락된 local payload를 확인했다. | runtime snapshot의 remote·local snapshot/admitted/dropped 세부 누계를 채우고 publish result와 직접 대조한다. |
| MON-B2 | 부분 구현 | 수락 가능한 local target과 막힌 local target을 함께 두고 publish result의 local snapshot·admitted·dropped, `multicast_dropped` event와 dropped 누계를 확인했다. | runtime snapshot의 remote·local snapshot/admitted/dropped 세부 누계를 채우고 publish result와 직접 대조한다. |
| MON-C1 | 부분 구현 | application callback을 대기시킨 동안 별도 request completion과 weight 전이, application·infrastructure claim 활성 상태, 정상 observer 진행, 느린 observer 예외 격리, drop 누계와 snapshot resync를 확인했다. | `claim_changed` event와 명시적인 sequence gap 검증을 추가한다. |
| MON-D1 | 10.0.0 전환 대상 | 비양수 polling interval과 없는 socket·Spot source가 구성·startup에서 실패하고, 한 번의 service down/up 경로가 있다. | 등록하지 않은 MeshName·0 이하 observer capacity 오류와 비정상 종료·lease 만료·재시작 3회의 sequence·snapshot·event field 제한을 검증한다. |

## 실행 증거

- 명령: `ZLINK_LOCAL_PACKAGE_ROOT=/tmp/zlink-java-validation-1784476567 ./run_e2e.sh all`
- 결과: `MON-A1`~`MON-A5`, `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1` 모두 통과,
  `monitoring e2e result=passed`
- 로그: `logs/20260720-013848-1763980/`

전체 runner 통과는 현재 scenario의 회귀 증거다. 표에서 `부분 구현` 또는 `10.0.0 전환 대상`으로 남긴
항목은 공통 Config7의 세부 gate를 아직 모두 단언하지 않으므로 완료 증거로 사용하지 않는다.
