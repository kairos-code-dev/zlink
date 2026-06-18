# GameQuest TypeScript Sample

GameQuest 샘플은 gameplay API가 플레이어 행동을 받고 quest mission 역할이 진행도를 갱신하는 흐름을 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 지역 입장, 몬스터 처치, 아이템 수집, 미션 완료를 순서대로 검증한다.
- `Server`는 game API와 quest mission 책임을 분리해 보여준다.
- `Shared`는 quest 진행도와 패킷 이름을 정의한다.

## Success Condition

클라이언트가 `PASS GameQuest.Ts`를 출력하면 quest 진행도 갱신과 완료 조건이 검증된 것이다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 샘플 파일, runner 연결, public API 경계를 확인한다.
