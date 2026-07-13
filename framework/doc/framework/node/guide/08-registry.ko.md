# Location topology 조회

Node framework는 별도 Registry 서버나 원격 Registry query client를 공개하지 않는다. 서버 자동
연결과 Spot·actor 위치 조회는 등록한 location store를 통해 수행한다. 애플리케이션이 현재 runtime의
topology를 확인해야 하면 framework가 제공하는 location runtime query를 사용한다.

## 1. location store 등록

```ts
const builder = zlinkFramework()
  .addLocationStore(redisLocationStore); // 이 실행이 사용하는 공유 location store를 등록한다.
Object.assign(builder.configureLocations(), {
  pollingIntervalMs: 1_000, // watch가 없을 때 location 변경을 확인하는 주기다.
});
const options = builder.build();
```

샘플과 E2E에서는 공식 Redis location store extension을 사용한다. Redis endpoint와 key prefix의
격리, container 수명 관리는 runner가 맡고 애플리케이션 코드는 Docker를 직접 제어하지 않는다.

## 2. runtime query

NestJS 애플리케이션은 `ZLINK_LOCATION_RUNTIME_QUERY` token으로
`ZLinkLocationRuntimeQuery`를 주입받는다. query는 framework가 이미 유지하는 location 상태를
읽으며 별도 원격 Registry 연결을 만들지 않는다.

## 회귀 테스트

location store와 runtime query는 `location-runtime.test.js`, `location-filter-parity.test.js`와
공통 E2E location 시나리오에서 확인한다.
