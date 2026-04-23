[계획 문서](./README.ko.md) | [실행 계획](./dotnet-execution-plan.ko.md)

# Framework .NET Release Gate And Failure Triage

이 문서는 `framework/languages/dotnet` release gate를 유지보수자가 어떻게 읽고,
실패가 나면 어떤 순서로 좁혀야 하는지 정리한다.

public API 계약 문서가 아니라 운영과 유지보수 기준 문서이므로, 현재 구현 상태와
CI gate 흐름을 기준으로 적는다.

## 1. gate 목적

`framework-dotnet.yml`의 목적은 아래를 한 번에 확인하는 것이다.

- `net8.0`, `net10.0` 기준 빌드와 테스트가 모두 유지되는가
- 여섯 RID(`win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`,
  `osx-arm64`)가 하나도 빠지지 않았는가
- `unit`, `integration-single-process`, `integration-multi-process`가 모두 실제 job에
  연결되어 있는가
- `bindings/dotnet` 변경이 framework gate를 다시 깨뜨리지 않는가

즉 이 gate는 "패키지가 빌드되는가"만 보는 것이 아니라, framework가 올린 DI,
lifecycle, monitoring, topology 계층이 그대로 유지되는지 확인하는 마지막 판정이다.

## 2. 현재 gate 구성

현재 workflow는 아래 순서로 돈다.

1. solution restore
2. `Debug` build
3. `Release` build
4. `Zlink.Framework.Tests` `net8.0 Debug`
5. `Zlink.Framework.Tests` `net10.0 Release`
6. `Zlink.Framework.MultiProcessTests` `net8.0 Release`
7. `Zlink.Framework.MultiProcessTests` `net10.0 Release`
8. matrix 전체 성공 뒤 `release-gate` job 통과

RID는 matrix로 고정한다.

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `osx-x64`
- `osx-arm64`

하나라도 빠지면 release gate가 닫히지 않은 것으로 본다.

## 3. 로컬 재현 명령

CI에서 깨졌을 때는 먼저 같은 명령을 로컬에서 재현한다.

```bash
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Release -f net10.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net10.0
```

multi-process만 다시 좁히고 싶으면 filter를 함께 쓴다.

```bash
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0 --filter FullyQualifiedName~TopologyMultiProcessTests
```

## 4. 실패 triage 순서

### 4.1 build 단계 실패

먼저 아래 둘을 나눈다.

- public contract나 registration surface가 깨졌는가
- low-level `bindings/dotnet` 경계가 깨졌는가

확인 위치는 아래가 우선이다.

- `src/Zlink.Framework/Configuration/`
- `src/Zlink.Framework/Runtime/`
- `src/Zlink.Framework.AspNetCore/`
- `bindings/dotnet/src/Zlink/`

nullable 경고가 새로 생겼다면 먼저 컴파일 의미가 흔들린 지점을 잡고, warning suppress로
덮지 않는다.

### 4.2 single-process 테스트 실패

single-process 실패는 보통 아래 셋 중 하나다.

- registration / startup validation drift
- hosted service lifecycle drift
- same-host dispatch, timer, stream, monitoring 의미 drift

먼저 실패한 테스트 이름을 보고 아래 파일부터 연다.

- registration: `RegistrationValidationTests.cs`
- lifecycle: `LifecycleHostedServiceTests.cs`
- channel/spot/stream: 각 `IntegrationTests.cs`
- monitoring/registry: `MonitoringIntegrationTests.cs`, `RegistryIntegrationTests.cs`

### 4.3 multi-process 테스트 실패

multi-process 실패는 먼저 harness 문제인지 topology 문제인지부터 나눈다.

1. `TestHostReadinessTests`가 깨졌는지 본다.
2. child process readiness timeout이면 `tests/Common/TestHostProcess.cs`,
   `FrameworkTestEnvironment.cs`, `testapps/Zlink.Framework.TestHost/Program.cs`를 먼저 본다.
3. readiness는 되는데 topology가 안 붙으면 registry/discovery endpoint와 bind endpoint를
   다시 본다.
4. spot peer/subject가 안 보이면 `UseSpotDiscovery(...)`, `EnablePubSub()`,
   `AddSpotFactory(...)`, `CreateAsync(...)` 순서가 맞는지 본다.

현재 harness는 `stdin` EOF가 아니라 `stop-file` 기반 종료를 기본으로 쓴다.
이렇게 해야 test host가 anonymous pipe 상태에서 startup을 붙잡지 않고, orphan process도
남기지 않는다.

### 4.4 monitoring / polling diff 실패

monitoring 실패는 source mismatch와 snapshot diff를 분리해서 본다.

- source mismatch: 등록 이름과 runtime source 이름이 다르다
- polling diff 실패: registry/spot snapshot은 잡히지만 변화 이벤트를 못 올린다

확인 순서는 아래가 빠르다.

- `ZLinkMonitoringHostedService.cs`
- `ZLinkFrameworkRuntime.cs`
- `ZLinkRegistryRuntime.cs`
- 관련 `MonitoringIntegrationTests.cs`

## 5. orphan process 확인

multi-process 테스트 뒤에는 아래 프로세스가 남지 않아야 한다.

- `Zlink.Framework.TestHost`
- `dotnet ... Zlink.Framework.TestHost.dll`
- `testhost.dll`

Linux에서는 아래 명령으로 본다.

```bash
pgrep -af 'Zlink\.Framework\.TestHost|testhost\.dll|Zlink\.Framework\.Tests'
```

`TestHost`가 CPU를 계속 먹는다면 먼저 `Program.cs`의 EOF/stop-file 종료 경로와
`TestHostProcess.DisposeAsync()` cleanup 경로를 같이 본다.

## 6. 문서 갱신 원칙

release gate와 테스트가 바뀌면 아래 문서도 같이 본다.

- `regression-test-matrix.ko.md`
- `dotnet-execution-plan.ko.md`
- 샘플이나 public surface가 바뀐 경우 관련 draft spec 문서

코드와 문서가 충돌하면 draft 문서를 먼저 고치고, 그 다음 구현과 테스트를 맞춘다.
