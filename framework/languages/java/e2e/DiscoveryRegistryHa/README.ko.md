# Java DiscoveryRegistryHa E2E

이 디렉터리는 공통 E2E `config-6-discovery-registry-ha.ko.md`를 Java framework public API로
검증한다. `.NET` 기준 구현과 같은 책임 분리를 유지하기 위해 Gradle subproject를 역할별로 나눈다.

## 구성

- `Shared`: client와 server가 함께 쓰는 메시지, HTTP helper, 환경 변수 helper를 둔다.
- `Server/Registry`: embedded registry process와 registry query HTTP endpoint를 제공한다.
- `Server/Provider`: channel provider와 provider evidence endpoint를 제공한다.
- `Server/Consumer`: public `ZLinkClient` discovery 경로로 request를 보내는 HTTP consumer다.
- `Server/Embedded`: registry와 provider를 한 process에 함께 띄우는 embedded 배포 모델이다.
- `Server/Probe`: remote registry query client를 쓰는 별도 probe application이다.
- `Client`: scenario 이름을 받아 `Client/Scenarios`의 대응 scenario class를 실행한다.

## 실행

```bash
./run_e2e.sh
```

runner는 `installDist`를 먼저 실행하고, 필요한 registry/provider/consumer/embedded process를
시나리오별로 띄운다. 실패하면 `logs/<run-id>/` 아래 role stdout, stderr, flow log를 출력한다.

## 현재 검증 범위

최근 확인한 전체 실행은 `logs/20260629-164645-539461`이다.

- 통과: `DR-A1`, `DR-A2`, `DR-A3`, `DR-A4`, `DR-B1`, `DR-C3`, `DR-D1`, `DR-D2`, `DR-D3`, `DR-D4`
- runtime gap marker: `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`

gap marker는 scenario를 완료로 간주한다는 뜻이 아니다. 공통 E2E가 요구하는 public 동작을 Java
runtime이 아직 같은 수준으로 보장하지 못하거나, 검증 harness가 `.NET`과 같은 의미를 아직 담지 못한
항목을 다음 작업 입력으로 남긴 것이다. 자세한 이유는 `feature-map.ko.md`와
`porting-inventory.ko.md`에 둔다.
