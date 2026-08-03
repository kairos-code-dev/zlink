# Go binding 자체 검토 승인 기록

이 기록은 사용자의 명시 요청에 따라 Go binding Core 11 작업을 구현자 관점에서 다시 검토하고
승인한 결과다. 구현자가 수행한 검토이므로 독립 frontier review를 수행했다고 주장하지 않는다.

## 승인 범위

- Go source revision: `427fbce0f5c0a3b6000506380b3d40521ed86413`
- Candidate manifest SHA-256: `d318525a4cf8496b6bef5d900c9a88330ea6d7e10ed4120ac0fd9f19d23f6765`
- Go source manifest SHA-256: `3240b10c68ad6dfb1ebe08a8ec27a6ea526a3b02ff48f59ed5c20b0573a59cff`
- Go package evidence SHA-256: `4ae453178ceb1a7bcaebe8994a39479eac14a86a9f8146642c503df7006888a2`
- Package: `zlink.systems/zlink/v11@v11.1.0`
- Package platform: `linux-x86_64`

`bindings/go`는 위 source revision 이후 변경되지 않았다. 따라서 이번 검토는 해당 source snapshot,
현재 Go runtime path, package evidence와 public contract 경계를 대상으로 했다.

## POSD·DDD 위험 신호와 판정

기존 자체 설계 검토에서 식별한 위험 신호를 다시 확인했다.

- request progress를 native handle 전역 map에 보관하는 구조는 제거되고 `socketCore`가 handle 단위
  progress owner가 되었다.
- callback handle 교체와 socket close는 같은 owner mutex 아래에서 등록·분리 순서를 결정한다.
  callback dispatcher 종료는 mutex 밖에서 수행하므로 callback 내부의 self-close 순환 대기를 만들지 않는다.
- request마다 native poller를 만들지 않고 handle 단위 progress pump를 공유한다. hot-path 비용 inventory의
  required 항목과 guard test가 연결되어 있으며 미분류 비용은 없다.
- service projection, Core 10 compatibility export, private cgo surface와 raw 우회가 Go public surface에
  남아 있지 않다.

위 항목에서 현재 source와 public contract를 즉시 막는 Critical, High, Medium finding은 확인하지 않았다.
공통 submit 반환 초안의 error-only 권고는 아직 승인된 contract가 아니므로 현재 `(bool, error)` signature를
임의로 바꾸지 않았다.

## 재검증 결과

다음 명령은 모두 exit code `0`으로 완료했다.

```text
go test ./...
go test -race ./...
go vet ./...
go test ./internal/native -run 'Test(OptimizationGuard|RawCore11Allowlist|HotPathCostInventory)' -count=1
./tests/run_tests.sh                         # samples: pass=7 fail=0
go test -race -run 'Test(CloseInsideReceiveCallbackDoesNotDeadlock|ReceiveCallbackPanicDoesNotCloseSocketOrStopFutureCallbacks|StreamOnPacketCanonicalRoundTrip)' -count=5
```

package evidence는 `cleanConsumer: pass`를 기록하고, source manifest와 candidate identity를 위 값으로
연결한다. 현재 Core candidate의 review는 `passed`이지만 `reviewer: coordinator_self_review`,
`independent: false`다. 이 review identity를 Go 독립 review로 재사용하지 않는다.

## 승인 판정

판정은 **`APPROVED (self-review scope)`**다. 이는 Go source, POSD·DDD refactoring, Linux x86_64 package,
clean consumer와 현재 test evidence를 자체 검토 범위에서 승인한다는 뜻이다.

다음 조건은 이 승인에 포함되지 않으며 전체 계획을 `COMPLETE`로 바꾸지 않는다.

1. PGR-COMMON-03의 Go·Rust submit 반환 및 Context/callback completion contract 승인
2. 동일 source manifest에 대한 구현자와 분리된 independent frontier review
3. 동일 Core candidate의 Linux arm64와 macOS native package·clean consumer 검증

따라서 전체 Go 계획의 상태는 계속 `PARTIAL / NOT CLEAN`이다.
