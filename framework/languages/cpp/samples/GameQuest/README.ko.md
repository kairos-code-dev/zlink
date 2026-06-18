# GameQuest C++ Sample

GameQuest 샘플은 플레이어 행동이 quest mission 진행도로 이어지는 흐름을 C++ 샘플 구조로 보여준다.

## 실행

```bash
./run_sample.sh
```

## Topology

- `Client`는 지역 입장, 몬스터 처치, 아이템 수집, 완료 조건을 검증한다.
- `Server`는 game API와 quest mission 책임을 분리한다.
- `Shared`는 quest 진행도 계약을 정의한다.

## Success Condition

클라이언트가 `gamequest=completed`를 출력하면 quest 완료 조건이 검증된 것이다.

## 회귀 테스트

`test_cpp_framework_sample_parity`와 CMake sample target이 샘플 구조와 빌드를 확인한다.
