# WSL 성능 시험 안정성 점검

이 문서는 `bindings/c/perf` 실행 뒤 WSL이 종료되거나 응답하지 않는 경우 확인할 항목을 정리한다.
2026-07-18 C multi/single 전체 실행 뒤 발생한 사례를 조사한 결과도 함께 기록한다.

## 1. 2026-07-18 조사 결과

확인된 사실은 다음과 같다.

- multi 결과 `perf_c_multi_linux_20260718_175134.txt`는 160개 case와 800개 RESULT line을 모두
  기록했고 status가 complete였다.
- single 결과 `perf_c_single_linux_20260718_181626.txt`도 모든 case를 기록하고 complete로 끝났다.
- 직전 WSL boot는 19:03에 끝났고, 다음 boot에서는 journal의 비정상 종료와 파일시스템 복구 흔적이
  확인됐다.
- 직전 kernel journal에는 OOM killer 기록이 남아 있지 않았다. journal 손상이 있었으므로
  기록이 보존되지 않았을 가능성은 있지만, 이번 사례를 OOM으로 확정할 증거는 없다.
- Windows Resource Exhaustion Detector에는 해당 시간대 event 2004가 없었다.
- Windows host에는 19:24에 reboot 요청이 기록됐으며 WSL은 19:25에 다시 시작됐다.

따라서 이번 사례에서 확인된 결론은 “perf 실행은 완료됐고 그 뒤 WSL이 비정상 종료됐다”까지다.
Core 10.0.0 버그나 OOM을 직접 원인으로 확정할 수 없다.

추가 검증 중 현재 WSL boot의 kernel log에서는 약 27초 간격으로
`systemd-journald: Time jumped backwards, rotating`이 반복됐다. 이 현상은 epoch wall-clock을
사용하는 MeshNode lifecycle generation 회귀 test의 “10ms를 기다리면 generation이 반드시 커진다”는
가정을 깨뜨렸고 실제 간헐 실패로 재현됐다. test는 두 process의 실제 generation을 비교해서, 더 크면
재연결을 검증하고 이전 값 이하면 계약에 정의된 `ESTALE` 충돌을 검증하도록 수정했다.

이 clock 보정 기록은 해당 test flake의 직접 증거지만, 2026-07-18 WSL 종료의 원인이라는 증거는
아니다. 현재 boot에서도 OOM killer 기록은 없고 메모리·swap 여유가 있는 동안 clock 보정이
반복됐다. 따라서 clock 보정, OOM과 WSL 종료를 서로 같은 원인으로 묶어 판단하지 않는다.

## 2. 확인한 메모리 위험

기존 C perf의 latency sampler는 active record의 지연 시간 값을 `std::vector`에 제한 없이
추가했다. 처리량이 높은 5초 case를 여러 크기로 실행하면 한 process의 sample 저장 공간이 계속
증가할 수 있었다. single one-way도 기본 생성자가 같은 방식으로 제한 없이 sample을 보관했다.

현재 기본 상한은 다음과 같다.

| 범위 | 환경 변수 | 기본 상한 |
|---|---|---:|
| multi 공통 sampler 하나 | `PERF_MULTI_LATENCY_SAMPLE_CAP` | 65,536 |
| single 공통 sampler 하나 | `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 1,000,000 |
| multi Spot child 하나 | 고정 reservoir | 1,024 |

상한에 도달하면 reservoir sampling으로 기존 sample을 교체한다. 전체 record 수와 지연 시간 합계는
계속 누적하므로 throughput과 평균 지연 시간은 sample 상한의 영향을 받지 않는다.
이 변경은 메시지 queue, HWM, 송신 간격, 동시 요청 수를 바꾸지 않는다.

## 3. Core 10.0.0 Spot 경로 확인

새 MeshNode 기반 Spot 패턴을 `tcp`, 64 bytes, 1초 조건에서 2, 10, 100 peer 순서로 실행했다.
100 peer 실행은 세 패턴 모두 complete였고 WSL 종료를 재현하지 않았다.

| 패턴 | runner와 자식 process RSS 합계 최고값 | 결과 |
|---|---:|---|
| `MULTI_SPOT_PUBSUB` | 약 603 MiB | complete |
| `MULTI_SPOT_REQREP` | 약 946 MiB | complete |
| `MULTI_SPOT_SENDSEND` | 약 993 MiB | complete |

실행 전후 WSL의 used memory는 약 13 GiB로 같았고 swap 증가는 없었다. 이 범위에서는 Core 10.0.0의
peer admission, Logical Multicast, direct Spot request/reply와 send/send 경로에서 지속적인 메모리
증가가 관찰되지 않았다.

추가로 100 peer pub/sub를 5초 동안 실행했을 때 각 peer mailbox가 채워지는 동안 process tree
RSS가 약 1.1 GiB까지 증가했다. 100개 peer process가 각각 MeshNode와 Spot mailbox를 가지므로 이
수치만으로 메모리 누수나 Core 버그를 확정할 수 없다. 실행 종료 뒤 메모리가 회수됐고 kernel OOM
기록도 없었다.

Logical Multicast에는 publish 전용 NODROP option이나 receiver staging을 두지 않는다. origin의 각
remote target은 일반 ROUTER의 HWM·send timeout·`DONTWAIT` 동작을 사용하고, receiver의 local Spot
mailbox가 가득 차면 해당 local 대상만 drop된다. remote ROUTER가 message를 수용했다는 결과를
receiver의 최종 Spot mailbox 수용 보장으로 확대하지 않는다. 여러 target을 미리 검사하거나
all-or-none으로 commit하지 않으며 먼저 수용된 target을 rollback하지 않는다.

따라서 위 RSS 관측을 publish 전용 staging이 필요한 버그 증거로 사용하지 않는다. 실제 버그 판정은
transport HWM에서 sender가 backpressure를 받는지, 각 mailbox가 유한 budget을 지키는지, phase와
process 종료 뒤 메모리가 회수되는지, 반복 실행에서 RSS·swap이 계속 증가하는지를 함께 확인한다.

MeshNode mailbox 자체도 profile로 계산한 유한 message/byte budget을 사용한다. 100개 peer process는
각각 별도 mailbox budget을 가지므로 process tree RSS 합계가 peer 수에 비례하는 것은 정상일 수
있다. 판정할 때는 개별 process RSS가 budget 부근에서 제한되는지, sender가 transport HWM에서
`EAGAIN`을 받는지, phase와 process 종료 뒤 메모리가 회수되는지를 함께 본다.

이 결과는 모든 transport와 큰 payload를 포함한 장시간 실행의 안전성을 증명하지 않는다. 전체
matrix를 실행할 때는 아래 절차로 다시 확인한다.

## 4. 재현과 진단 순서

1. `core/build/lib/libzlink.so`가 `core/src`와 `core/include`보다 최신인지 확인한다.
2. 한 패턴, `tcp`, 64 bytes, 1초, client 2개로 시작한다.
3. client를 10개와 100개로 늘리면서 process tree의 RSS 합계를 기록한다.
4. 64 bytes가 통과한 뒤 payload 크기를 하나씩 늘린다.
5. `tcp`가 통과한 뒤 `tls`, `ws`, `wss`를 각각 실행한다.
6. WSL이 종료되면 재시작 직후 다른 작업을 하기 전에 직전 boot log를 보존한다.

```bash
journalctl --list-boots
journalctl -k -b -1
journalctl -b -1
free -h
```

OOM이 발생했다면 kernel log에서 `oom-kill`, `Out of memory`, `Killed process`와 해당 process의
RSS를 확인한다. 이 문자열이 없으면 OOM으로 단정하지 않는다. journal 손상이 확인되면
Windows의 Resource Exhaustion Detector event 2004와 Hyper-V/WSL event log를 함께 확인한다.

## 5. 전체 실행과 OOM 처리 원칙

- 공식 workload의 전송률, payload, client 수, duration을 OOM 회피 목적으로 낮추지 않는다.
- runner의 사전 메모리 검사는 workload를 자동 축소하지 않는다. 가용 메모리가 부족하면 같은
  조건을 충분한 메모리가 있는 장비에서 실행하도록 `skip` 또는 `fail`로 기록한다.
- MeshNode process 토폴로지의 메모리 예상값은 실행 가능 여부와 모니터링 기준에만 사용한다.
  전송 간격이나 peer 수를 바꾸는 입력으로 사용하지 않는다.
- Spot peer context의 I/O thread 기본값은 하나다. 이는 peer 하나에 MeshNode 하나를 배치하는
  기준 구성이고 OOM 회피용 전송 제한이 아니다. 비교 목적이면
  `PERF_MULTI_SPOT_NODE_IO_THREADS`로 명시적으로 바꾼다.
- latency sample은 bounded reservoir로 유지한다.
- runner는 실제로 사용한 `core/build` runtime 경로를 출력해야 한다.
- 실패한 case를 자동 재시도하지 않는다. 메모리 증가와 Core 오류가 가려질 수 있기 때문이다.
- 각 payload phase 전에 context auto-HWM message unit을 현재 payload 크기로 갱신한다.
- Core가 auto-HWM과 backpressure로 burst를 제한하지 못해 OOM이 발생하면 perf를 제한하지 않고
  Core 회귀 test와 수정으로 해결한다.
