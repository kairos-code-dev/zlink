<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

[Node.js 묶음](../README.ko.md) | [Backend Policy](backend-dependency-policy.ko.md)

# ZLink Framework Node.js Runtime Lifecycle

## 1. 목적

이 문서는 NestJS lifecycle hook과 framework runtime host의 내부 배선만 설명한다.
사용자가 관찰하는 validation, timeout, cancellation과 reconnect 계약은 각 기능
spec이 소유한다.

## 2. 시작 순서

1. `ZLinkModule`이 options와 발견한 handler metadata로 registration을 만든다.
2. registration validator가 channel, Spot, stream과 handler 조합을 검사한다.
3. backend adapter factory와 runtime host를 만든다.
4. location store와 자동 연결 runtime을 시작한다.
5. channel, route, Spot과 stream runtime을 시작한다.
6. monitoring source를 준비된 runtime에 붙인다.

시작 도중 실패하면 이미 만든 adapter와 runtime을 역순으로 정리하고 NestJS module
초기화를 실패시킨다. 부분적으로 시작된 runtime을 provider로 노출하지 않는다.

## 3. 종료 순서

NestJS module destroy가 시작되면 runtime stop signal을 전달한 뒤 monitoring, Spot,
route, stream, channel, location, backend context 순서로 정리한다. listener와 pending
Promise는 각 runtime 소유자가 완료하거나 reject한다. event loop를 blocking wait로
점유하지 않는다.

## 4. 책임 경계

- NestJS provider는 DI 구성과 lifecycle hook 연결만 담당한다.
- framework runtime host는 등록된 runtime 객체와 stop signal을 소유한다.
- backend adapter는 binding public API 호출과 native resource 정리를 소유한다.
- application handler와 sample은 binding internal/native/generated 경로를 알지 않는다.

## 5. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `test/contract/nestjs-module.test.js` | runtime host 시작과 종료가 idempotent하며 등록한 runtime 자원을 정리한다. |
| `test/contract/location-host.test.js` | runtime host가 location runtime과 자동 연결 loop의 lifecycle을 연결한다. |
| `test/contract/backend-contract.test.js` | backend adapter가 자신이 만든 runtime 자원을 정리한다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:bottom:end -->
