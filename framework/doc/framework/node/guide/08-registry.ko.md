# Registry — topology 조회

registry 는 service topology 를 관리하고 discovery 의 기준점이 된다. Node 버전은
embedded registry 와 remote query client 를 모두 제공한다.

## 1. embedded registry

```ts
ZLinkRegistryModule.forRoot({
  pubEndpoint: 'tcp://0.0.0.0:5550',
  routerEndpoint: 'tcp://0.0.0.0:5551',
});
```

topology, member peers 를 조회한다. query 는 lazy startup 의미를 가진다.

## 2. remote query client

```ts
  endpoint: 'tcp://registry:5551',
});
```

remote query client 는 topology snapshot 만 제공한다. 이는 하부 binding의 public API
폭과 같다.

## 회귀 테스트

registry runtime 과 query client 는 `test/contract/registry-runtime.test.js` 에서
확인한다.
