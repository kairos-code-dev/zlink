# C++ Resilience/Lifecycle E2E feature map

이 문서는 Config 5 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 구현한 시나리오

- `RL-A1`: provider를 종료한 뒤 같은 endpoint와 같은 rid로 다시 띄우고, 실행 중인 consumer가
  재시작 없이 follow-up request를 성공하는지 검증한다.
- `RL-B1`: 느린 request를 짧은 timeout으로 실패시킨 뒤, 같은 consumer의 후속 request가 late
  reply에 오염되지 않고 정상 동작하는지 검증한다.
- `RL-B3`: provider 하나를 정상 종료하고, 실행 중인 consumer가 남은 provider로 request를
  계속 성공시키는지 검증한다.

## C++에서 제외한 시나리오

- `RL-B4`, `RL-B5`: C++ framework public API에는 실행 중 channel server socket weight를
  바꾸는 runtime accessor가 없다. bindings socket option을 직접 만지는 우회는 framework public
  surface 검증이 아니므로 사용하지 않는다.
- `RL-A3`, `RL-B2`, `RL-B6`, `RL-C1`, `RL-C2`, `RL-C3`, `RL-C4`, `RL-D1`, `RL-D2`,
  `RL-D4`, `RL-D5`: 현재 C++ E2E harness에 지속 부하, SIGKILL 중 in-flight 관측,
  registry TTL 단축, network partition, runtime error sink 관측을 안정적으로 고정하는 전용
  제어 표면이 아직 없다. 해당 제어가 생기면 별도 scenario로 확장한다.
- `RL-A2`, `RL-A4`, `RL-A5`: Config 1 runner가 scale-out/in과 same-rid endpoint 교체를 일부
  검증하지만, Config 5의 반복 복구·rolling/blue-green 판정에는 더 긴 traffic harness가 필요하다.
