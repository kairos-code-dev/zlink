<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Backend Policy](backend-dependency-policy.ko.md) | [공개 lifecycle 계약](../../spec/server/languages/dotnet/01-system-structure.ko.md)

# ZLink Framework .NET Runtime Lifecycle

## 1. 목적

이 문서는 `.NET` framework 유지보수자가 host와 내부 runtime의 시작·종료 배선을
파악할 때 필요한 순서만 설명한다. 사용자가 관찰하는 오류, timeout, cancellation,
reconnect 계약은 각 기능 spec이 소유하며 여기서 반복하지 않는다.

## 2. 시작 순서

host가 시작될 때 내부 구성은 다음 순서로 준비된다.

1. 등록 정보와 handler 노출 조합을 검증한다.
2. backend context와 framework runtime state를 만든다.
3. location store, owner lease와 자동 연결 loop를 준비한다.
4. channel, route channel, SpotNode, SPOT bridge와 stream node를 시작한다.
5. monitoring source가 실제 runtime을 참조할 수 있는지 확인하고 event 수집을 붙인다.
6. 모든 단계가 끝난 뒤 host 시작을 완료한다.

설정 오류는 runtime 객체를 부분적으로 사용하기 전에 검증한다. 시작 도중 실패하면
이미 만든 runtime을 역순으로 정리한다.

## 3. 종료 순서

종료 시에는 새 작업의 진입을 막기 위해 runtime stop token을 먼저 취소한다. 이후
내부 객체를 다음 순서로 정리한다.

1. monitoring source를 분리한다.
2. SpotNode와 SPOT route bridge를 정리한다.
3. route channel과 stream node를 정리한다.
4. client, publisher, subscriber, server channel bundle을 정리한다.
5. location watcher와 자동 연결 loop를 중지하고 owner row를 정리한다.
6. backend context를 dispose한다.

pending submit queue는 dispose 과정에서 남은 항목을 예외로 완료한다. queue를 기다리기
위해 호출자 thread를 점유하지 않는다. 공개 호출자가 처리해야 하는 오류 종류는
channel과 stream 책임 spec을 따른다.

## 4. 책임 경계

- `ZLinkFrameworkHostedService`는 ASP.NET Core host lifecycle과 runtime을 연결한다.
- `ZLinkFrameworkRuntimeState`는 runtime stop token과 내부 runtime 객체의 소유권을
  가진다.
- channel, Spot, stream runtime은 자신의 listener와 pending 작업을 스스로 정리한다.
- location hosted service는 store lifecycle과 자동 연결 loop를 소유한다.
- monitoring hosted service는 다른 runtime이 준비된 뒤 source를 붙이고 가장 먼저
  분리한다.

이 순서를 public API의 호출 전제 조건으로 노출하지 않는다. 사용자는 host를 시작하고
중지하는 표준 ASP.NET Core lifecycle만 사용한다.

## 5. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:RL-C1` | 여러 framework host를 생성하고 종료한 뒤 follow-up request가 성공해 host lifecycle 정리를 검증한다. |
| `LocationRuntimeTests.Shutdown_Removes_Owner_Lease_Then_Bulk_Removes_Rows` | location 종료가 owner lease와 row를 정해진 순서로 정리한다. |
| `ZLinkAsyncSubmitterTests.DisposeAsync_FailsPendingItems` | runtime dispose가 pending submit을 남겨 두지 않는다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:bottom:end -->
