# Kotlin RegistryMessaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`

Kotlin 구현은 Java runtime 위의 Kotlin 사용 표면을 검증한다. Client는 framework client를 직접 들지
않고 실제 role server의 HTTP endpoint를 호출한다. framework request/send/route 호출은 Provider,
Consumer, Workflow role server endpoint 내부에서 public API로 수행한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | implemented | Redis location store 자동 연결로 discovery consumer request를 보내고, public location peer row와 provider evidence를 검증한다. |
| RM-A2 | implemented | provider role의 manual endpoint 경로로 `api-a` 도달을 검증한다. |
| RM-A4 | implemented | Redis location store 동적 cluster에서 같은 rid provider v1/v2를 교체하고 discovery consumer가 peer row 제거/재등장과 instance 전환을 검증한다. |
| RM-A6 | implemented | discovery consumer가 API channel과 workflow channel을 각각 Redis location store 자동 연결로 호출하고 channel별 evidence를 확인한다. |
| RM-B1 | implemented | Redis location store 동적 cluster에서 provider B 추가 뒤 discovery consumer가 public peer row 2개와 두 provider routing을 확인한다. |
| RM-B2 | implemented | Redis location store 동적 cluster에서 provider B 종료 뒤 public peer row 제거와 survivor `api-a` routing을 검증한다. |
| RM-C1 | implemented | request reply와 send command marker를 provider evidence에서 확인한다. |
| RM-C2 | implemented | provider role endpoint가 route mesh target rid request를 수행하고 missing rid 실패를 반환한다. |
| RM-C3 | implemented | direct consumer role endpoint가 두 provider endpoint를 직접 붙여 batch request 분산을 검증한다. |
| RM-C4 | implemented | discovery consumer role endpoint가 timeout을 관측하고 후속 request evidence를 확인한다. |
| RM-C5 | implemented | missing packet request/send 뒤 dispatch-error evidence와 정상 request 회복을 확인한다. |
| RM-C7 | implemented | Redis location store 동적 cluster에서 provider weight를 public runtime option으로 설정하고 discovery consumer가 high-weight provider 선호를 검증한다. |
| RM-C8 | implemented | 1B, 4KiB, 256KiB, 1MiB payload 왕복을 length와 SHA-256 hash로 검증한다. MaxMessageSize 초과 거부는 framework channel runtime 미배선 한계로 완료 주장하지 않는다. |
| RM-C9 | implemented | 느린 provider에 다량 one-way send를 제출하고, public bounded-failure oracle 없이 backlog 해소 뒤 후속 request와 evidence가 회복되는지 검증한다. |

## 검증 결과

- `RM-C8` 통과 범위는 payload round-trip이다. MaxMessageSize 초과 거부는 .NET feature-map도
  framework channel runtime 미배선 한계로 별도 한계라고 기록하므로 Kotlin에서 완료로 확장하지 않는다.
- `RM-C9`는 one-way send pressure와 recovery를 검증한다. 직접적인 HWM 오류 결과 검증은 binding/runtime 내부 테스트
  범위로 둔다.
- `logs/20260704-025128-60467`: `timeout 1200s framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh all`
  실행 결과 모든 `RM-*` scenario marker와 `registry-messaging kotlin e2e result=passed`를 확인했다. 이 실행은
  legacy registry role을 빌드하거나 시작하지 않고 Redis location store endpoint와 key prefix만 provider/consumer/workflow
  role에 전달한다. `all` runner는 scenario별 isolated sub-run을 사용해 동적 scenario와 common role이 같은 Redis prefix를
  공유하지 않는다.
