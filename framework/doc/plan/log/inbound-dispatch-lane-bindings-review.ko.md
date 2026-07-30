# Inbound dispatch lane bindings review 기록

## Round 1

Candidate `9f08fdefec`을 Core candidate `6985cf1a61` 위에 고정하고 Codex만 사용해
`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, 언어별 binding spec과 local
package contract를 전체 검토했다.

| Reviewer | Model | Reasoning | Report | 판정 |
| --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | `/tmp/zlink-review-results/bindings-codex-round1.md` | `NOT CLEAN` |

Finding은 `Medium` 2건과 `Low` 1건이다. C++와 Node가 monitor ABI version·size mismatch를
검증하지 않고 v2 snapshot을 공개하던 문제, Node context uint64 getter가 native 반환 길이를
검증하지 않고 stack buffer를 복사하던 문제다.

두 가지 대안을 비교했다. ABI mismatch를 TypeScript나 public result 생성 뒤 검증하는 방법은
native code가 layout의 trailing field를 먼저 읽으므로 안전하지 않다. Native-to-contract 변환
경계에서 exact ABI version과 size를 검사하는 방법을 선택했다. Context getter도 가변 heap
buffer로 일반화하지 않고 uint64 전용 계약에 맞춰 exact 8-byte 결과만 허용한다. 이 방식은
추가 allocation이나 public option을 만들지 않는다.

Candidate `37f4f394b1`에서 세 finding을 모두 반영했다. C++ contract 10/10, Node HWM contract
2/2와 raw test 31/31이 통과했다. 사용자 지시에 따라 이 Round 1 finding 반영 뒤 추가 review
cycle은 수행하지 않는다. 따라서 Round 1 report를 `CLEAN`으로 바꾸지 않으며, finding 전체 반영과
회귀 검증을 bindings review 종료 증거로 사용한다.

.NET binding의 final Core 재검증 중에는 별도의 use-after-free도 확인했다. Private request pump가
socket과 context 종료 뒤 native poller를 호출하던 race를 socket close 전 worker stop·join으로
수정했다. Public surface는 바뀌지 않았고 재현 test 5/5와 전체 suite 132/132가 두 번 연속
통과했다. Native stack은 `/tmp/zlink-dotnet-core-gdb.log`, 전체 결과는
`/tmp/zlink-dotnet-full-after-pump-fix.log`와
`/tmp/zlink-dotnet-full-after-pump-fix-repeat.log`에 있다.
