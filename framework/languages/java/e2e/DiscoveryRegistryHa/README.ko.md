# Java DiscoveryRegistryHa E2E

이 디렉터리는 이름은 아직 `DiscoveryRegistryHa`이지만, 현재 포팅 기준은 공통 E2E
`config-6-store-failure-recovery.ko.md`다. registry/discovery HA 검증이 아니라 Redis location
store 장애와 복구 동작을 public framework API로 검증한다.

## 구성

- `Shared`: client와 server가 함께 쓰는 메시지, HTTP helper, 환경 변수 helper를 둔다.
- `Server/Provider`: channel provider와 provider evidence endpoint를 제공한다.
- `Server/Consumer`: location store 자동 연결로 request를 보내고, public location runtime query를
  HTTP endpoint로 노출한다.
- `Client`: scenario 이름을 받아 검증을 실행한다.

`Server/Registry`, `Server/Embedded`, `Server/Probe`는 제거된 public registry 계약에 의존하던
old DR role이라 소스에서 제거했다.

## 실행

```bash
./run_e2e.sh SF-A1
./run_e2e.sh SF-A2
./run_e2e.sh SF-B1
./run_e2e.sh SF-B2
./run_e2e.sh SF-C1
./run_e2e.sh SF-C2
./run_e2e.sh SF-D1
./run_e2e.sh SF-D2
./run_e2e.sh SF-D3
./run_e2e.sh all
```

인자를 생략하면 현재 전환된 baseline인 `SF-A1`을 실행한다. 실패하면 `logs/<run-id>/` 아래 role
stdout, stderr, flow log를 출력한다.

## 현재 검증 범위

- 통과: `SF-A1`, `SF-A2`, `SF-B1`, `SF-B2`, `SF-C1`, `SF-C2`, `SF-D1`, `SF-D2`, `SF-D3`, `all`
- 남음: 없음

Config 6 범위는 public HTTP harness, runtime query, 실제 messaging 결과를 함께 사용해 검증한다.
