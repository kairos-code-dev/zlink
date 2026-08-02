# .NET Framework runtime regression evidence

이 기록은 2026-08-02 14:15 KST의 현재 working tree에서 실행한 runtime regression 결과다.
다른 언어와 Core에 이미 존재하는 dirty 변경은 이 실행의 범위에 포함하지 않았으며, 이 결과만으로
process E2E 완료를 추론하지 않는다.

## 실행 조건

- 대상: `framework/languages/dotnet`
- build: `--no-restore --no-incremental --maxcpucount:1`
- test: `--no-build --no-restore --blame-crash --blame-crash-dump-type mini`
- package: `./scripts/verify_packaged_contract.sh`

## 결과

| Gate | 결과 | 의미 |
|---|---:|---|
| `Zlink.Framework.ContractTests` | `76/76` | exact interface, source/package export와 contract snapshot |
| Relocation/HWM targeted UnitTests | `305/305` | `DrainCoordinatorTests`, `ActorHandoffTests`, `StandaloneActorRelocationRuntimeTests`, `MaintenanceRuntimeTests`, `EntrySpotActorDispatchTests`, `InboundDispatchOptionsTests`, `TopologyExactSurface` |
| `Zlink.Framework.UnitTests` | `1408/1408` | 현재 runtime source의 전체 unit regression, testhost crash·timeout 없음 |
| Runtime build | error `0`, warning `0` | nullable warning을 포함한 compiler warning이 없다 |
| `verify_packaged_contract.sh` | exit `0` | 9개 NuGet package manifest/dependency, clean consumer와 standalone HTTP consumer |
| Public API snapshot | 동일 | `399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a` |

Package assembly hash는 다음과 같다.

```text
Zlink.Framework.Contracts=7a37b937a3c18dc05d97e02b677b166904eedbe72e7f59b02f39fca9fbc7f218
Zlink.Framework.Provider.Abstractions=a2e039b0bdf26e50509d4e711e9d4af7de53ca490c25c3a790a027c25a68427d
Zlink.Framework=31707f59982467573a91096f81aa1d306601afeb152ebd326a2cc4b2eeb7df85
Zlink.Framework.AspNetCore=4135740862af75ded0485e2b401b3b1502e1d80aa0e7bed5ddbf1e8820d14c0f
Zlink.Framework.Codecs.MessagePack=90f716928f43be200de65c51b1b30962734f635bd29f9c923b7ac6d853b5d526
Zlink.Framework.Codecs.Protobuf=1bfa82b807c1843113d47731c2e39d09804b2bf265031281c6d71a6e5677a674
Zlink.Framework.Locations.Redis=c6212fd0f3cee06057da4b989108dc4b126ef28d9b09704051bc8924707ca006
Zlink.HttpClient=0d291c73f0a62dd6ab58f2f6628ecb3249546722e6b60e75aab3eb37a623ef8a
Systems.Zlink.Stream.Connector=62dac1d066570dc17fe8cce83d5a296d16f4e642ecea1d031237c332894725c8
```

이 결과로 source·contract·unit·package gate의 현재 실행 증거는 갱신되었다. mixed-topology,
cross-process relocation/Join, deadline failure, slow observer와 Config 14 process fixture는 별도
process E2E gap으로 남아 있다.
