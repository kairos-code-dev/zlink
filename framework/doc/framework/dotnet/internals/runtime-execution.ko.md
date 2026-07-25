<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Runtime Lifecycle](runtime-lifecycle.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) |
[Session Actor Dispatch 계약](../../common/spec/server/languages/dotnet/interfaces/07-stream-session.ko.md)

# ZLink Framework .NET Runtime Execution

## 1. 목적

이 문서는 session, actor와 Spot handler의 실행 순서를 만드는 내부 queue 소유권을
설명한다. public API와 사용법은 session actor dispatch와 Spot spec이 소유한다.

## 2. 실행 경계

runtime은 transport callback에서 application handler를 직접 호출하지 않는다.
transport 진입점은 실행 항목을 해당 queue에 넣고, managed task가 handler를 실행한다.

| 입력 | 내부 직렬화 경계 |
|------|------------------|
| 같은 stream session의 lifecycle과 packet | session 실행 queue |
| Entry Spot 또는 아직 user Spot에 속하지 않은 actor packet | actor mailbox |
| user Spot의 packet, actor packet, timer와 subscription | user Spot 실행 queue |
| 서로 다른 actor 또는 서로 다른 user Spot | 서로 독립된 queue |

actor가 이동할 때 dispatch 위치는 queue 대기 전에 고정하지 않는다. 실행 차례가 왔을
때 현재 actor 위치를 다시 확인해야 이전 Spot으로 stale dispatch가 발생하지 않는다.

## 3. 작업 완료와 오류 관측

queue 항목은 실행 callback과 completion을 함께 가진다. request는 handler가 reply나
오류를 만들 때까지 completion을 기다리고, one-way 작업은 enqueue 이후 호출자를
해제할 수 있다. 호출자가 completion을 기다리지 않는 handler 예외도 runtime error
sink가 관찰해야 하며 unobserved task exception으로 남겨서는 안 된다.

Host admission seal 뒤에는 새 application 항목을 받지 않는다. Seal 전에 수락한 항목은 accepted journal
boundary에 포함하며 request terminal completion과 infrastructure work는 deadline까지 계속 진행한다. Queue
정리를 기다리기 위해 raw socket callback thread나 호출자 thread를 점유하지 않는다.

## 4. Relocation execution

Actor·User Spot aggregate와 Instance Spot relocation coordinator는 `Preparing`, `Captured`, `Prepared`, `Committed`, `Activating`,
`Activated`, `Cleaning`, `Completed`, `Aborted` phase를 내부 state machine으로 처리한다. Owner와 phase는
`IZLinkAuthorityStore.CompareExchangeAuthorityAsync(...)` 한 호출로 바꾸고, `Committed` 뒤에는 source owner로
rollback하지 않는다.

`Recreate`와 `Snapshot`은 accepted journal·timer state를 Relocation Store에 immutable payload로 기록한다.
`Snapshot`만 adapter가 반환한 opaque `byte[]` application state를 추가한다. Runtime은
`TimeSpan.FromHours(24)` retention을 넘기고 current Location authority가 가리키는 reference만 읽는다.
`Get/Missing`은 authority를 다시 읽어 stale reference와 relocation data loss를 구분하고 `Delete/Missing`은
idempotent cleanup으로 끝낸다.

Target lease가 commit 전에 만료되면 reservation과 target을 CAS로 교체한다. Commit 뒤 activation 전에
만료되면 recovery coordinator가 authority revision, old target generation과 transaction generation을 모두
비교해 successor를 고른다. Application callback은 CAS, relocation reference, journal cursor와 phase를 받지
않는다.

## 5. 정보 은닉

queue 구현 타입, channel 자료구조, maintenance barrier와 task scheduling 방식은 internal이다.
spec과 application 예제는 이러한 타입 이름을 사용하지 않고 다음 공개 의미만
설명한다.

- 같은 session callback의 순서
- actor와 user Spot의 실행 직렬성
- handler cancellation과 오류 관측
- actor 이동 뒤 현재 위치에서 dispatch되는 의미
- opaque state capture·restore와 terminal relocation 결과

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `SerialExecutorTests.SerialExecutionQueue_AutomaticTurn_Allows_Later_Work_Then_Resumes_On_Line` | framework 비동기 호출을 기다리는 동안에도 단일 실행 줄의 순서와 재개 위치를 유지한다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot 전체를 하나의 queue로 묶지 않고 actor별 mailbox를 사용한다. |
| `SerialExecutorTests.SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work` | 기다리는 호출이 취소돼도 이미 등록한 작업을 임의로 제거하지 않는다. |
| `SerialExecutorTests.StreamSessionSerialExecutor_Continues_After_Work_Exception` | session 작업 오류를 관찰하고 다음 작업을 계속 실행한다. |
| `AuthorityRelocationTests.Owner_And_Phase_Use_One_ExpectedVersion_Cas` | owner와 phase가 한 opaque CAS payload로 바뀐다. |
| `RelocationStoreTests.Missing_Delete_Is_Idempotent_And_Retention_Is_24Hours` | missing 결과와 fixed retention을 검증한다. |
| `RelocationRecoveryTests.Replaces_Target_With_All_Fences` | recovery target replacement가 authority·node·transaction generation을 모두 비교한다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Runtime Lifecycle](runtime-lifecycle.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:bottom:end -->
