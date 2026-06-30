# GameQuest C++ Sample

GameQuest 샘플은 플레이어 행동이 quest mission 진행도로 이어지는 흐름을 C++ 샘플 구조로 보여준다.
구조와 호출 표면은 `.NET` GameQuest 샘플을 기준으로 맞춘다. Client는 HTTP API로 플레이어
행동을 제출하고 stream connector로 quest 진행 알림과 조회 요청을 처리한다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 HTTP와 stream connector를 사용해 지역 입장, 몬스터 처치, 아이템 수집, 완료 조건을
  검증한다.
- `Server/Registry`는 discovery registry를 실행한다.
- `Server/GameApi`는 gameplay HTTP API와 quest stream session을 제공한다.
- `Server/QuestMission`은 gameplay fanout subscriber 역할을 담당한다.
- `Shared`는 quest 진행도 계약을 정의한다.

## Success Condition

클라이언트가 `gamequest=completed`를 출력하면 quest 완료 조건이 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
