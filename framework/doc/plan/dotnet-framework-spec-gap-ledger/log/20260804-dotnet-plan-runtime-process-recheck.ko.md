# 2026-08-04 .NET implementation plan runtime·process 재검증

## 현재 판정

현재 .NET source의 runtime 수정과 unit·contract·source regression gate는 통과했다. `common/internals`
대조에서 확인한 owner-layer 수정도 대표 process와 Instance Spot idle process에서 다시 확인했다.
다만 Config 14의 나머지 시나리오, mixed-language process, 전체 sample runner의 ZoneWorld 경로와
구조·성능 audit은 닫히지 않았다. 따라서 `dotnet-implementation-plan.ko.md`와 통합 ledger를 전체
완료로 표시하지 않는다.

이번 기록에서 `bindings/dotnet/samples`의 MeshNode·Spot·Actor 디렉터리는 검증 대상에서 제외했다.
사용자 확인에 따라 RouteMesh 10.0.0 이후 남은 build 불가 고아 코드이며, 현재 .NET Framework 작업의
public contract나 runtime path가 아니다.

## 1. 현재 source와 package gate

| 검증 | 결과 |
|---|---:|
| 전체 `Zlink.Framework.UnitTests` | `1505/1505`, failed 0 / skipped 0 |
| `Zlink.Framework.ContractTests` | `76/76` |
| `Zlink.Framework.SampleRegressionTests` | `145/145` |
| UnitTests project build | warning 0 / error 0 |
| `framework/languages/dotnet/scripts/verify_packaged_contract.sh` | exit 0 |

Package verifier가 확인한 snapshot hash는
`81942c6b3c47374bab5979a4c655592956a9a2d2de0b1333b828f20e1b656b`이다. 이 결과는 source test와
새 package clean consumer를 각각 확인하지만, 전체 sample runner의 process 결과를 대신하지 않는다.

## 2. Config 14 process evidence

`framework/languages/dotnet/e2e/InstanceSpot/run_e2e.sh`는 현재 `IS-E2E-01`~`IS-E2E-03`과
`IS-E2E-08`을 `SpotService` role process로 위임한다. 다음 selector는 fresh process 실행에서
통과했다.

| Scenario | 확인한 동작 | evidence |
|---|---|---|
| `IS-E2E-01` | cold activation admission | `framework/languages/dotnet/e2e/SpotService/logs/20260804-145911-582473/` |
| `IS-E2E-02` | accepted send | `framework/languages/dotnet/e2e/SpotService/logs/20260804-150030-587658/` |
| `IS-E2E-03` | concurrent first-request ownership | `framework/languages/dotnet/e2e/SpotService/logs/20260804-150104-591087/` |
| `IS-E2E-08` | idle eviction 뒤 같은 Spot ID의 cold reactivation | `framework/languages/dotnet/e2e/SpotService/logs/20260804-165721-1053531/` |

`IS-E2E-04`~`IS-E2E-07`, `IS-E2E-09`~`IS-E2E-36`은 아직 독립 process runner를 구현하지 않았다. `all`은 이를 성공으로 세지 않고 exit 2로
fail-closed 한다. 현재 실행 결과는 다음과 같다.

```text
InstanceSpot 'all' is not executable yet.
The .NET process fixture currently covers IS-E2E-01 through IS-E2E-03 and IS-E2E-08.
The aggregate runner keeps Config 14 incomplete until the remaining scenarios
have their own process evidence.
exit=2
```

남은 process 조건은 owner process 종료 뒤 takeover 금지, generation 경계, relocation·close/reactivate,
store·capacity·deadline 실패와 ordinary-message 경쟁이다.

## 3. 대표 process matrix

다음은 같은 날 fresh 실행에서 확인한 대표 경로다.

| 경로 | 결과 | evidence |
|---|---:|---|
| `LocationMessaging:RM-C1` | PASS 3회 연속 | `framework/languages/dotnet/e2e/LocationMessaging/logs/20260804-151159-624467/`, `20260804-151637-642662/`, `20260804-151704-644935/` |
| `ChannelEgressRouting:CH-E2E-03` | PASS | `framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260804-151221-626528/` |
| `SpotService:sm-e2-e3` | PASS | `framework/languages/dotnet/e2e/SpotService/logs/20260804-151237-628321/` |
| `PubSub:PS-B1` | PASS | `framework/languages/dotnet/e2e/PubSub/logs/20260804-151313-632318/` |
| `ChannelEgressRouting:CH-REG-02` | PASS | `framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260804-151338-635494/` |

이 대표 실행은 RouteMesh, ClientServer, service, STREAM, fanout의 owner path를 확인한다. 전체
cross-language matrix와 command 50 mixed-process relay는 아직 실행하지 않았다.

## 4. 전체 sample runner의 현재 상태

전체 sample runner는 build와 앞선 sample을 통과했지만 ZoneWorld process 경로에서 안정적으로
완료되지 않았다. TicTacToe는 단독 runner와 선택 sample aggregate에서는 통과했다.

```text
bash framework/languages/dotnet/samples/run_samples.sh
```

전체 runner에서는 `ZW-B5` 또는 `ZW-B4`의 `ZoneStateNotify` 대기 timeout 뒤 replacement node
경로에서 native `fast_mutex` abort와 `TaskCanceledException`이 관찰됐다. 이 결과를 전체 sample
성공으로 합산하지 않는다. 선택 sample aggregate는 exit 0으로 통과했다.

앞선 TicTacToe 실행에서 확인한 one-way send와 actor cleanup 순서 문제는 request/reply handler와
서버 lifecycle cleanup의 취소 토큰 경계를 고쳐 해결했다. 단독 TicTacToe runner와 선택 sample
aggregate가 통과했으며, 그 과정에 임의의 delay·retry·raw frame 처리·새 codec을 넣지 않았다.
현재 전체 runner의 blocker는 TicTacToe가 아니라 ZoneWorld replacement node 경로다.

## 5. common/internals 대조 결과

- dispatch loop에서는 content type에 따른 codec 선택과 concurrent first-use cache를 유지한다.
  등록은 runtime 시작 전에 끝나고, receive worker가 같은 serializer cache를 안전하게 읽도록 했다.
- liveness와 state에서는 observer snapshot을 주기적으로 polling하지 않고, state change signal과
  bounded observation queue를 사용한다. ClientServer의 10ms snapshot polling 경로를 제거했다.
- message ownership에서는 binding 경계의 snapshot을 유지하면서 `ZLinkMessageRuntime`의 중복
  `ToArray()`를 제거했다. public `ZLinkEncodedPayload`의 defensive copy 계약은 유지한다.
- serialization과 progress isolation에서는 application·lifecycle lane, count·byte HWM, lifecycle
  turn limit과 owner time slice를 유지한다.
- service wire protocol에서는 command 50 codec에 version, kind, flags, hop, record count와 byte
  bound 검사를 넣고, bound 검사 전에 allocation하지 않는 경로를 확인했다.
- timer는 logical `ZLinkTimer` entry를 하나의 generation scheduler로 처리한다. Spot-node idle
  maintenance에는 node catalog별 `PeriodicTimer`가 남아 있다. 이는 logical timer gate와
  별도의 maintenance 경로이며, `IS-E2E-08` process에서 실제 close·재활성화를 확인했다.

구조 점검에서는 monitoring model을 세 파일로 나눴고, serial queue의 priority reservation debt를
제거했다. 그러나 relocation driver consolidation, ManagedMeshNode split, interface의 단일 구현
목록 정리와 D2·D4·D6 측정 audit은 아직 모두 완료되지 않았다.

## 6. 문서와 공개 트리 검사

다음 검사는 통과했다.

```text
bash scripts/verify-framework-doc-contracts.sh
FRAMEWORK DOC CONTRACTS CLEAN version=11.0.0
```

공개 문서에서 plan 링크를 찾는 검사와 AGENTS.md §4의 금지 표현 검사는 출력이 없었다.
대상 .NET source와 `framework/doc/plan/for-interals`의 `git diff --check`도 통과했다. 전체
worktree의 기존 C++ whitespace는 이 .NET 작업의 변경으로 간주하지 않았다.

`mkdocs build --strict -f doc/site/mkdocs.yml`는 public plan 링크 때문이 아니라 기존 상대 경로와
nav 설정 문제에 대한 약 140개 경고로 실패했다. 사이트 전체 gate는 별도 미완료 조건이다.

## 7. 남은 완료 조건

다음 조건은 이번 실행 결과에 합산하지 않는다.

1. Config 14 `IS-E2E-04`~`IS-E2E-07`, `IS-E2E-09`~`IS-E2E-36`의 process proof
2. 전체 sample runner의 ZoneWorld process 안정성 및 운영 경계
3. mixed-language command 50 relay와 공통 cross-language process matrix
4. `ST-I4`, `OBS-C6`, RuntimeMonitoring 일부와 `ZW-B6` operational harness
5. logical timer maintenance를 포함한 D2·D4·D6 성능·측정 audit
6. 독립 final audit과 `mkdocs build --strict` 사이트 gate

따라서 현재 판정은 runtime·unit·contract·package와 일부 process는 통과, 전체 implementation plan은
미완료다.

## 8. 16:57 후속 재검증

파일 이동 뒤 정식 `InstanceSpot` 진입점으로 다음 명령을 다시 실행했다.

```text
bash framework/languages/dotnet/e2e/InstanceSpot/run_e2e.sh IS-E2E-08
exit=0
operation SpotService.instance-idle passed
spot-service client operation_group=instance-idle result=passed
```

로그 `framework/languages/dotnet/e2e/SpotService/logs/20260804-165721-1053531/play-a.evidence.log`에는
동일 Spot ID에 대해 첫 `instance-initialize`와 request, `reason=IdleEvicted` closing, 두 번째
`instance-initialize`와 request가 순서대로 기록됐다. 이 결과로 idle eviction process proof는
닫혔지만 Config 14 전체와 공통 cross-language matrix의 판정은 바뀌지 않는다.

후속 source gate는 전체 UnitTests `1505/1505`, ContractTests `76/76`, SampleRegressionTests
`145/145`, packaged contract verifier exit `0`이다. 전체 sample runner는 ZoneWorld의
`ZoneStateNotify` timeout과 native `fast_mutex` abort 때문에 아직 PASS가 아니다.

## 9. 19:00 수신 progress 수정 후 fresh package 재검증

앞선 전체 sample 실행에서 `ZW-B4`는 단독 실행에서는 통과했지만 전체 부하에서는 gateway가
route seal reply를 제출한 뒤 source node의 `RequestToNode` completion이 deadline 안에 처리되지
않았다. target node의 pending admission이 먼저 만료되었으므로, sample에 delay·retry를 넣지 않고
수신 owner인 .NET runtime을 수정했다.

`ZLinkManagedMeshNode`의 raw receive loop가 application HWM의 bounded classification allowance를
모두 사용한 경우 `AcquireReceiveAsync`를 기다리지 않고 poller로 돌아가도록 했다. 이 대기 때문에
application payload가 HWM을 넘은 동안 Core request completion과 infrastructure control이 같은
poller에서 지연되지 않게 하는 변경이다. `InboundDispatchBudgetTests`에는 allowance가 가득 찬
상태에서 `TryAcquireReceive`가 즉시 반환하고 lease 해제 뒤 다시 수신을 허용하는 회귀를 추가했다.

소스 기준 재검증 결과는 다음과 같다.

```text
UnitTests              1508/1508 PASS
ContractTests             76/76 PASS
SampleRegressionTests    145/145 PASS
```

동일한 Core provenance와 runtime을 사용해 fresh package를 다시 만들고 clean consumer를 통과시켰다.

```text
package=/tmp/zlink-dotnet-package-B4fix/nuget/Systems.Zlink.11.2.0.nupkg
package_sha256=cb835d74cc15ab60c86be704bfc28ba9430849b81b70817c293e4545435cd68b
core_runtime_sha256=24f61f8869a32edf1a6d70334600e7999c4cdd2babcc65044dc4b5e7690ffe52
clean_consumer=pass
```

fresh package로 다음 전체 runner를 실행했다.

```text
ZLINK_LOCAL_PACKAGE_ROOT=/tmp/zlink-dotnet-package-B4fix \
NUGET_PACKAGES=/tmp/zlink-dotnet-nuget-B4fix-run2 \
DOTNET_CLI_HOME=/tmp/zlink-dotnet-home-B4fix-run2 \
bash framework/languages/dotnet/samples/run_samples.sh
exit=0
logs=/tmp/tmp.hXnlD5iAMU/logs
```

TicTacToe, Bingo, SupportChat, ShoppingMall, DeliveryDispatch, GameQuest와 ZoneWorld의 실행 가능한
scenario batch가 통과했다. ZoneWorld에서는 `ZW-B4`가 full-load 실행 중에도 통과했고, 이전에
관찰한 `ZoneChangedNotify` timeout과 native abort는 재현되지 않았다. 반면 `ZW-B6`는 이전 owner
route를 주입할 수 있는 지원 harness가 없어 실행되지 않았고, runner는 이를 성공으로 표시하지
않았다. 따라서 전체 sample runner의 실행 가능한 범위는 통과했지만 `ZW-B6` 운영 증거와 전체
implementation plan의 완료 판정은 아직 닫지 않는다.

## 10. 19:17 Channel·STREAM 수신 경로와 B4fix2 재검증

`common/internals/03-progress-isolation`과 `07-dispatch-loop`을 다시 대조한 결과, 수신
application HWM이 같은 poller의 response·liveness·runtime control 진행을 막을 수 있는 경로가
Channel과 STREAM에도 남아 있었다. `ZLinkChannelReceiveLoop`의 ClientServer·subscriber·fanout
loop와 `ZLinkStreamNodeRuntime`의 새 raw frame 경계가 `TryAcquireReceive`를 사용하도록 고쳤다.
허용량이 없으면 application payload를 기다리지 않고 poller로 돌아가며, STREAM multipart의
중간 part는 기존 raw receive permit의 범위에서 계속 처리한다. `ZLinkManagedMeshNode`의 raw
socket loop에도 같은 규칙이 적용되어 있다.

이번 수정은 다음 internals 조건을 직접 만족시키는 owner-layer 변경이다.

- 처리 대기 byte가 상한에 도달하면 새 application 수신만 멈춘다.
- response·liveness·runtime control은 application HWM 대기 때문에 같은 receive worker에서
  정지하지 않는다.
- raw receive classification reservation은 고정된 bounded allowance로 제한하고, control은
  분류 뒤 즉시 반환하며 application은 terminal 상태까지 payload 회계를 유지한다.
- poller가 깨어난 뒤 ready 상태를 다시 확인하는 기존 bounded loop와 multipart 경계를 유지한다.

변경 뒤 source gate는 다음과 같다.

```text
UnitTests              1508/1508 PASS
ContractTests             76/76 PASS
SampleRegressionTests    145/145 PASS
```

동일한 Core provenance로 fresh package와 clean public consumer를 다시 확인했다.

```text
package=/tmp/zlink-dotnet-package-B4fix2/nuget/Systems.Zlink.11.2.0.nupkg
package_sha256=1d576eccecdc7abf4d55139e9c41dcc8d96c934fc9c1afae67c47c905c7f3247
core_runtime_sha256=24f61f8869a32edf1a6d70334600e7999c4cdd2babcc65044dc4b5e7690ffe52
clean_consumer=pass
```

같은 package를 지정한 실행 결과는 다음과 같다.

| 경로 | 결과 | evidence |
|---|---:|---|
| 전체 .NET sample runner | 실행 가능한 scenario PASS, `ZW-B6` withheld | `/tmp/tmp.ZqY1VC61Bm/logs/` |
| `ChannelEgressRouting:CH-REG-02` | PASS | `e2e/ChannelEgressRouting/logs/20260804-191404-1716136/` |
| `PubSub:PS-B1` | PASS | `e2e/PubSub/logs/20260804-191426-1717520/` |
| `LocationMessaging:RM-C1` | PASS | `e2e/LocationMessaging/logs/20260804-191449-1719226/` |
| `SpotService:sm-e2-e3` | PASS | `e2e/SpotService/logs/20260804-191734-1724645/` |

전체 sample runner는 exit 0이지만 `ZW-B6`를 성공으로 세지 않는다. 이전 owner route를
주입하는 operational harness가 없으므로, global Actor API를 이용한 지연·재시도나 새 public
API를 추가해 해당 scenario를 통과시키지 않았다.

현재 Node와 같은 의미의 runtime·unit·package·실행 가능한 sample gate는 .NET에도 확보했다.
그러나 Node 쪽의 refactor 완료와 동일한 상태라고 판정하지 않는다. .NET에는
`ZLinkManagedMeshNode`의 책임별 분해, relocation driver 통합, 단일 구현 interface 정리,
Config 14 나머지 process 증거, mixed-language command 50 relay와 공통 topology matrix,
D6 측정 audit, `ZW-B6` harness가 남아 있다.

## 11. 19:33 channel selection 책임 분리 후 재검증

Node 쪽 refactor 완료와 비교할 수 있도록 .NET의 구조 부채도 기능 코드와 분리해 점검했다.
먼저 `ZLinkManagedMeshNode`가 직접 보유하던 channel selection plan·cursor·retained current와
declared-channel 판정을 `Runtime/Service/ZLinkMeshChannelSelection.cs`로 추출했다. MeshNode는
peer topology를 `ZLinkMeshChannelTarget` snapshot으로 만들고, selection owner는 plan lifecycle과
candidate 선택만 담당한다. 기존 smooth weighted selection, RID tiebreak, retained current와
multicast candidate 동작은 변경하지 않았다. 이 변경은 단순히 파일을 나눈 것이 아니라 selection
상태의 소유자를 별도 모듈로 옮긴 것이다.

refactor 뒤 source gate는 다음과 같다.

```text
UnitTests              1509/1509 PASS
ContractTests             76/76 PASS
SampleRegressionTests    145/145 PASS
```

직접 test host를 single-process 조건 없이 실행했을 때 native host가 459개 뒤 중단되었으나,
같은 source를 `-f net8.0 --no-build --no-restore --maxcpucount:1 --blame-crash`로 다시 실행해
전체 `1509/1509`을 확인했다. 첫 실행의 부분 통과를 전체 결과로 합산하지 않는다.

refactor를 포함한 fresh package와 clean consumer는 다음 결과다.

```text
package=/tmp/zlink-dotnet-package-B4refactor/nuget/Systems.Zlink.11.2.0.nupkg
package_sha256=39aa0d6409ba8867909f0d18aa93e874c2c75c5aa5a628ff63b143e40e90aeaa
core_runtime_sha256=24f61f8869a32edf1a6d70334600e7999c4cdd2babcc65044dc4b5e7690ffe52
clean_consumer=pass
```

같은 package의 process evidence는 다음과 같다.

| 경로 | 결과 | evidence |
|---|---:|---|
| 전체 .NET sample runner | 실행 가능한 scenario PASS, `ZW-B6` withheld | `/tmp/tmp.HtoR8a7mrE/logs/` |
| `ChannelEgressRouting:CH-REG-02` | PASS | `e2e/ChannelEgressRouting/logs/20260804-193133-1772252/` |
| `PubSub:PS-B1` | PASS | `e2e/PubSub/logs/20260804-193133-1772264/` |
| `LocationMessaging:RM-C1` | PASS | `e2e/LocationMessaging/logs/20260804-193133-1772288/` |
| `SpotService:sm-e2-e3` | PASS | `e2e/SpotService/logs/20260804-193133-1772310/` |

process 재검증 중 네 runner가 공통 source output을 동시에 build하면서 2회의 MSBuild 파일
잠금 warning을 기록했지만 error는 없었고 각 process 결과는 exit 0이었다. 다음 재검증부터는
공통 source build와 process 실행을 직렬화해 warning 없는 evidence를 남긴다.

이번 refactor로 channel selection 한 책임은 분리했지만, .NET이 Node와 같은 전체 refactor
완료 상태가 된 것은 아니다. `ZLinkManagedMeshNode`의 relocation·peer admission·operation
dispatch 분리, relocation driver 통합, 단일 구현 interface 정리, Config 14 나머지 process
증거, mixed-language command 50 relay와 공통 topology matrix, D6 측정 audit 및 `ZW-B6`
harness가 남아 있다.

## 12. 20:28 stale descriptor snapshot 재조회 후 재검증

앞선 `ZW-B4` 실행에서 target descriptor가 local publication보다 오래된 Store snapshot으로
읽혀 live node 목록에서 제외될 수 있었다. 이전 revision을 수용하면 stale route가 재활성화될
수 있으므로, older revision을 계속 거부하는 규칙은 유지하고 같은 owner의 revision 경쟁으로
판정된 경우에만 resolver가 새 snapshot을 최대 3회 다시 읽도록 수정했다. retired owner로
판정된 row는 재조회로 수용하지 않는다.

변경 위치는 `ZLinkObservedLocationGenerations.AcceptDescriptor`와
`ZLinkStoreLocationResolvers.ListLiveMeshNodesAsync`다. `LocationResolverTests`의 회귀 검사는
revision 2가 관찰된 뒤 revision 1 snapshot이 먼저 반환되는 상황에서 두 번째 list 호출이
현재 row를 선택하는지 확인한다.

소스 기준 검증 결과는 다음과 같다.

```text
UnitTests              1510/1510 PASS
ContractTests             76/76 PASS
SampleRegressionTests    145/145 PASS
```

현재 source로 다시 만든 package와 clean consumer 결과는 다음과 같다.

```text
package=/home/hep7/zlink-dotnet-package-B4resolver1/nuget/Systems.Zlink.11.2.0.nupkg
package_sha256=a991b760e53fbf27d1ca789b4727708dfe4dd2474d3f7ef6cf95da9811bedbbc
core_runtime_sha256=24f61f8869a32edf1a6d70334600e7999c4cdd2babcc65044dc4b5e7690ffe52
clean_consumer=pass
```

동일 package의 process 검증도 다시 실행했다.

| 경로 | 결과 | evidence |
|---|---:|---|
| 직접 `ZoneWorld:ZW-B4` | PASS | `/home/hep7/zlink-dotnet-sample-tmp-B4resolver1/tmp.4pM5BCqLKm/logs/` |
| 전체 .NET sample runner | 실행 가능한 scenario PASS, `ZW-B6` withheld | `/home/hep7/zlink-dotnet-sample-tmp-B4resolver1-all/tmp.V5SQAilkvX/logs/` |

전체 runner의 최종 출력은 `zoneworld-border-sync=completed`와
`zoneworld-ops-*=completed`를 기록했으며, `ZW-B6`는 이전 owner route를 주입하는 지원
harness가 없어서 성공으로 세지 않았다. 따라서 stale snapshot으로 발생한 `ZW-B4` 경로는
현재 package에서 통과했지만, Config 14의 나머지 process 행, mixed-language command 50
relay와 공통 topology matrix, D6 측정 audit, `ZW-B6` harness 및 전체 구조 refactor는 아직
완료 조건이 아니다.

## 13. 20:45 peer admission 책임 분리

`ZLinkManagedMeshNode`가 직접 수행하던 admission 후보 선택과 duplicate connection 선택을
`Runtime/Service/ZLinkMeshPeerAdmission.cs`로 옮겼다. 이 모듈은 configured RID, physical RID,
endpoint, direction과 stable discriminator를 기준으로 후보를 선택하고, 후보가 둘 이상이면
임의의 dictionary 순서로 선택하지 않는다. socket 연결, peer index 변경과 admission state
변경은 기존 MeshNode owner에 남겨 정책과 자원 변경을 분리했다.

`ZLinkMeshPeerAdmissionTests`는 configured identity 우선 선택, unknown intent 모호성 거부,
admitted RID 기반 duplicate 선택을 확인한다. 이 변경 뒤 `ZLinkManagedMeshNode.cs`는
8,171줄이다. public interface와 wire 형식은 변경하지 않았다.

```text
ZLinkMeshPeerAdmissionTests  3/3 PASS
```

이 변경은 전체 source gate와 fresh package를 다시 만든 뒤 합산해야 하므로, 이 시점에는
targeted unit 결과만 기록한다. 다음 단계에서 Unit·Contract·SampleRegression, clean consumer와
전체 executable sample을 새 package 기준으로 다시 실행한다.
