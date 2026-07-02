# GameQuest 샘플

`GameQuest`는 게임 API 역할과 퀘스트 미션 역할을 분리해서 플레이어별 퀘스트
진행을 검증하는 .NET Framework 샘플이다. 클라이언트는 stream session에
연결하고, 서버는 ZLink channel과 session push를 사용해서 게임 이벤트 처리,
퀘스트 완료 알림, 진행 상태 재구성을 수행한다.

## 실행

Linux 또는 WSL에서 전체 시나리오를 실행한다.

```bash
./run_sample.sh
```

Windows PowerShell에서는 다음 명령을 사용한다.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```

## 구성

- `Shared/`는 클라이언트와 서버가 함께 쓰는 요청, 응답, 알림 계약을 담는다.
- `Client/`는 두 플레이어의 stream connector를 열고 self-check 시나리오를 실행한다.
- `Server/GameApi/`는 플레이어 session, 게임 이벤트 접수, client push를 담당한다.
- `Server/QuestMission/`은 퀘스트 진행 상태를 계산하고 완료 결과를 GameApi로 보낸다.
- 서버 프로세스들은 registry 없이 공유 location store(Redis)에 위치를 등록하고 자동 연결한다.
  `run_sample.sh`가 실행마다 전용 Redis 컨테이너를 띄우고 `GAMEQUEST_REDIS_ENDPOINT`와
  `GAMEQUEST_REDIS_KEY_PREFIX`를 주입한다.
- `Server/Configuration/`은 역할별 endpoint, channel, packet 설정을 모은다.

## 성공 조건

클라이언트 시나리오는 첫 사냥, 경매 개방, 멱등성, projection rebuild, 두 번째
플레이어 진행 동기화를 검증한다. 클라이언트 self-check가 실패하면 runner가
중단된다. 서버 evidence 확인이 끝나면 runner가
`gamequest-server-evidence=completed`를 출력한다.
