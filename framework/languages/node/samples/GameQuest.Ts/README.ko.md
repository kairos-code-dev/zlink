# GameQuest TypeScript Sample

GameQuest TypeScript 샘플은 .NET GameQuest 정본처럼 GameApi 역할과 QuestMission 역할을 분리한다. 클라이언트는 ZLink stream으로 session에 붙고, GameApi는 gameplay action을 검증한 뒤 player별 owner route로 QuestMission에 전달한다.

## 실행

```bash
cd framework/languages/node/samples/GameQuest.Ts
./run_sample.sh
```

`run_sample.sh`는 전용 Redis 컨테이너와 네 역할을 띄운 뒤 클라이언트 self-check를 실행한다. 성공하면 `PASS GameQuest.Ts`가 출력된다.

## Topology

```text
+------------------+     +------------------+
| Client Scenario  | --> | GameApi A/B      |
+------------------+     +------------------+
          |                       |
          | ZLink stream          | owner route by PlayerId
          v                       v
+------------------+     +------------------+
| Player Session   |     | QuestMission A/B |
+------------------+     +------------------+
                                  |
                                  v
                         +------------------+
                         | PlayerQuestSpot  |
                         +------------------+
```

GameApi는 stream session과 HTTP self-check endpoint를 제공한다. QuestMission은 route mesh와 spot mesh를 등록하고, player별 quest 진행을 공유 저장소에 기록한다. 샘플 저장소는 실행 디렉터리 아래 JSON 파일을 사용하므로 클라이언트 시나리오가 두 API와 두 mission 프로세스를 오가며 같은 상태를 확인할 수 있다.

## Success Condition

클라이언트 시나리오는 다음 조건을 모두 확인한다.

- `JoinSessionReq` 이후 같은 stream으로 사냥 진행 push와 완료 push를 받는다.
- 같은 idempotency key로 보낸 이벤트는 같은 event id로 처리된다.
- feature unlock, mission complete, area enter 이벤트가 owner route를 통해 처리된다.
- owner close self-check 뒤에도 다음 gameplay event로 진행이 이어진다.
- 두 번째 플레이어는 다른 API stream으로 접속해 offline progress를 복원하고 완료 push를 받는다.
- projection delete 이후 stream 조회에서 누락을 확인하고, rebuild self-check 뒤 projection을 다시 확인한다.
- owner 메시징 없이 만든 누락 gameplay fact는 `SyncQuestProgressReq`로 보정된다.
- 서버 evidence endpoint가 완료, rebuild, reconcile 증거를 모두 반환한다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`는 `GameQuest.Ts`를 required sample과 topology sample 목록에 포함한다. codec 책임은 framework 기본 JSON stream codec에 두며, 샘플은 `@zlink-systems/framework`, `@zlink-systems/nestjs`, `@zlink-systems/stream-connector`, `@zlink-systems/http-client` 공개 표면만 사용한다.
