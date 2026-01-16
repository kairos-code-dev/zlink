# ASIO syscall 최적화 계획 (2026-01-16)

## 1. 문제 재정의 (잘못된 가정 교정)

- ASIO 추상화 자체 오버헤드는 0.14%로 사실상 무시 가능.
- epoll_wait 과다 호출은 제거했지만 성능 개선은 +1.6%로 미미.
- 즉, "ASIO 추상화"나 "epoll_wait"이 핵심 병목이라는 가정은 폐기.

## 2. 실제 근본 원인

- read syscall 2배, write syscall 1.5배 더 많음.
- libzmq는 sendto/recvfrom 기반, zlink는 read/write 기반.
- 동일한 메시지 처리량 대비 syscall 빈도가 높아져
  ROUTER 계열 성능이 크게 하락.

## 3. 개선 전략 (4개)

### 전략 A: syscall 분석 (원인 파악 + 근거 확보)
- read/write 호출당 평균 바이트, EAGAIN/partial 비율을 계측.
- libzmq 대비 작은 버퍼, 재시도 루프, batching 부족 여부를 명확화.

### 전략 B: Proactor 핸들러 배칭 (read/write 호출 수 줄이기)
- `asio_engine::on_read_complete`에서 수신 버퍼의 다건 메시지를
  루프 처리하여 엔진-소켓 왕복을 감소.
- `process_output`에서 즉시 `async_write`를 호출하지 않고,
  출력 큐를 모아 `buffer sequence`로 묶은 뒤 단일 `async_write` 수행.
- 오버헤드가 큰 패턴(ROUTER 계열) 우선 적용.

### 전략 C: IPC 최적화 또는 TCP Scatter-Gather I/O 구체화
- IPC 경로에서 `stream_descriptor` 기반 read/write의
  작은 버퍼 및 재시도 루프를 줄이는 최적화 후보 점검.
- TCP 경로는 `writev`에 해당하는 ASIO buffer sequence 활용 여부를
  점검하여 syscall 호출 수 감소 가능성 확인.

### 전략 D: Scatter-Gather I/O 명시적 구현
- 헤더+바디 또는 다건 메시지를 `std::vector<boost::asio::const_buffer>`로
  묶어 단일 `async_write`로 전송.
- 단일 버퍼 복사(`memcpy`) 대신 buffer sequence를 사용해
  syscall 및 핸들러 호출 수를 동시에 감소.

## 4. 구현 계획 (Phase 1-4)

### Phase 1: syscall 정밀 계측
- 벤치 대상: DEALER_ROUTER, ROUTER_ROUTER (TCP 64B 기준).
- 항목: 평균 read/write 크기, EAGAIN 비율, partial 비율,
  read/write 호출 간격 및 패턴.
- 결과물: libzmq 대비 차이 원인 도출 (배칭/버퍼/재시도 중 어디서 발생하는지).
- 성공 기준:
  - 계측 커버리지 100% (read/write, send/recv 모두 포함).
  - syscall 감소율 목표: 0% (원인 파악 단계).
  - latency 증가 허용: +1% 이내.

### Phase 2: Proactor 배칭 개선
- `on_read_complete` 경로에서 소형 메시지 배칭 강화.
- `process_output` 경로에서 write 호출 묶기 실험.
- 성능 영향이 큰 ROUTER 계열에 우선 적용 후 확장.
- 실패 시 원인 기록 (latency 증가, 캐시 압박 등).
- 성공 기준:
  - syscall 감소율: read/write 합산 20% 이상.
  - latency 증가 허용: +3% 이내 (p50/p99).

### Phase 3: IPC 최적화 또는 TCP Scatter-Gather I/O 점검
- IPC 경로의 read/write 재시도 및 버퍼 크기 개선 여부 확인.
- TCP 경로는 buffer sequence 기반 `async_write`로 syscall 감소 가능성 검증.
- 동일 벤치마크에서 syscall 횟수 변화와 성능 개선 여부 비교.
- 성공 기준:
  - syscall 감소율: read/write 합산 30% 이상 또는 Phase 2 대비 추가 10%p 개선.
  - latency 증가 허용: +5% 이내 (p50/p99).

### Phase 4: Scatter-Gather I/O 명시적 구현
- 헤더/바디 및 다건 메시지 조합을 위한 buffer sequence 설계.
- `async_write` 단일 호출로 전송하도록 코드 경로 구현.
- 성공 기준:
  - syscall 감소율: Phase 3 대비 추가 10%p 개선.
  - latency 증가 허용: +5% 이내 (p50/p99).

## 5. 검증 전략

- 지표 1: read/write 호출 수 (strace 기준).
- 지표 2: 처리량 (bench 결과, libzmq 대비 %).
- 지표 3: ROUTER 계열 개선폭 (최소 +15~20% 목표).
- 지표 4: PAIR/PUBSUB/DEALER 회귀 여부.
- 최소 2회 이상 반복 측정, 변동성 표기.

## 6. 벤치 환경 조건 (표)

| 항목 | 값 |
| --- | --- |
| OS / 커널 | |
| CPU 모델 / 코어 | |
| CPU governor / SMT | |
| 메모리 | |
| NIC / 드라이버 | |
| MTU | |
| 빌드 타입 / 컴파일러 | |
| ZMQ 옵션 (CXX 표준 등) | |
| 벤치 도구 / 버전 | |
| 메시지 크기 / 패턴 | |
| 반복 횟수 | |
| 고정 핀닝 / NUMA | |

## 7. 계측 명세

- strace 옵션:
  - `strace -ff -tt -T -e trace=read,write,recvfrom,sendto,recvmsg,sendmsg -c`
  - 필요 시 `-o strace.log` 및 프로세스별 파일 유지.
- 수집 지표:
  - syscall 횟수: read/write/recv*/send* 별 count.
  - 평균/중앙값/상위 99% 소요 시간 (strace -c + p99 별도 계산).
  - 평균 read/write 크기 (bytes/call).
  - EAGAIN 및 partial 비율.
  - 호출 간격 패턴 (burst vs steady).

## 8. 회귀 테스트 항목

- 기존 bench 시나리오: DEALER_ROUTER, ROUTER_ROUTER, PUB_SUB, PAIR.
- 기능 테스트: `tests/test_*` 중 TCP 경로 포함 케이스.
- 안정성: 장시간 반복(run >= 30분)에서 error/메모리 증가 확인.
- 성능 회귀: 처리량 -5% 이상 하락 시 원인 분석.
- 플랫폼 확인: Linux x64 우선, 필요 시 macOS/Windows 체크.

## 9. IPC/TCP Scatter-Gather 전환 시 점검 항목

- socket 옵션 유지 여부 (TCP_NODELAY, SO_SNDBUF/SO_RCVBUF).
- EAGAIN/partial 처리 로직 동일성.
- ASIO 핸들러 호출 순서/스레드 안전성 영향.
- 기존 경로와 결과 비교 (기능/성능).
- TCP keepalive/linger 등 부가 옵션 유지 여부.

## 참고
- /home/ulalax/.claude/plans/staged-painting-hippo.md
- docs/team/20260116_asio-batching-optimization/experiment_results.md
- docs/team/20260116_epoll-wait-optimization/experiment_results.md
