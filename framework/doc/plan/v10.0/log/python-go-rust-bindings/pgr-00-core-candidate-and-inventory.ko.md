# PGR-00 Core candidate와 전환 inventory

## 1. 기록 범위

이 기록은 2026-07-20에 PGR 작업을 시작할 때 확인한 개발 입력이다. 아직 최종 package 후보가 아니다.
Core 소스와 공식 runtime의 일치가 확인되지 않았으므로 이 snapshot으로 package 완료나 review clean을
판정하지 않는다. Core 후보가 바뀌면 이 문서의 hash, ABI, 제거 대상과 perf 대응표를 다시 만든다.

## 2. Core 개발 snapshot

| 항목 | 값 | 판정 |
|---|---|---|
| Git revision | `23800d9c6b8a11f710c7cafbbdea68fd59bce2ab` | PGR 시작 revision |
| source version | `10.6.0` | 실행 진행표의 최종 목표 `10.7.0` 이전 개발 입력 |
| Core spec aggregate | `5e6b6e245bf7f5570ecfbc291b25b5387f1f78ed615db71f10b374f5414c4827` | `core/doc/spec/core/` |
| public header aggregate | `f8d248c332eb75c8d203516ba69681be9858b980f996bec35f6cc2ccffc2278d` | `core/include/` |
| source·header·spec aggregate | `3a0a2b205cfefc7a2e8daf369ffc307045491c902ca143e2a7cc428cf5558a2b` | commit하지 않은 Core 변경 포함 |
| runtime | `core/build/lib/libzlink.so.10.6.0` | SONAME `libzlink.so.10` |
| runtime SHA-256 | `d6307135291364f7799b3700b5d625ebf6ab344e00d9924f10a791f2002c433d` | 개발 중 기존 산출물 |
| exported `zlink_*` 목록 hash | `fe74809818a2ac28b4f5ba3c60d784762f94958405a41a796b4c5648bd80ef72` | 정렬한 dynamic export 이름 목록 |
| freshness | 실패 | `core/src/`에 runtime보다 새로운 파일이 있음 |

freshness 실패 때문에 native payload 동기화와 package 생성은 보류한다. 다른 작업이 사용 중인
`bindings/c/perf/run_benchmarks_multi.sh` 프로세스가 종료되고 Core 후보가 고정된 뒤
`cmake --build core/build`로 공식 runtime을 다시 만든다. 그때 `zlink_version`, ABI layout fixture,
runtime hash와 세 bindings에 복사한 payload hash를 추가로 고정한다.

aggregate는 각 tree root에 대한 상대 경로와 파일 내용 SHA-256을 정렬한 뒤 다시 SHA-256으로 묶는다.
절대 checkout 경로가 달라도 같은 tree는 같은 값이 되도록 한다. review 중 Core spec·source가 변경되어
초기 값을 폐기하고 위 값을 다시 고정했다. 이 값도 final candidate가 아니라 공통 도구 review 시점의
개발 입력이며, 공식 runtime을 다시 만들기 직전에 다시 계산한다.

## 3. 전환 대상 재검색

다음 검색은 source, test, sample, perf와 문서를 포함한다.

```bash
rg -l -e 'SpotNode|spot_node|SpotRouteBridge|spot_route_bridge|dispatch_workers|dispatch_worker' bindings/<lang>
```

| 언어 | 검색된 파일 수 | 해석 |
|---|---:|---|
| Python | 53 | 공개 export, contract, runtime, sample, test와 perf 전환 필요 |
| Go | 49 | public alias, native runtime, sample, test와 perf 전환 필요 |
| Rust | 47 | crate export, contract, runtime, sample, test와 perf 전환 필요 |

완료 검색은 위 이름 외에도 공개 service `*_part` wrapper와 이전 alias를 포함한다. 단순 문자열 no-hit만
사용하지 않고 Python import-fail, Go compile-fail, Rust compile-fail 계약 검사와 함께 판정한다.
정확한 파일 목록은 [제거 대상 파일 snapshot](./pgr-00-legacy-files.ko.md)에 고정했다.

## 4. C perf 대응 inventory

현재 runner의 기본 pattern 이름을 비교하면 세 언어 runner는 이전 이름과 축약된 pattern 집합을 사용한다.
따라서 현재 상태는 대응 차이 0개가 아니며 각 언어 lane의 red gate다.

| suite | C 기준 | Python | Go | Rust | 필요한 정렬 |
|---|---|---|---|---|---|
| single | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `DEALER_ROUTER_REQREP`, `ROUTER_ROUTER`, `ROUTER_ROUTER_REQREP`, `SPOT_PUBSUB` | 앞의 여섯 항목과 `SPOT` | 앞의 여섯 항목과 `SPOT` | 앞의 여섯 항목과 `SPOT` | 두 `*_REQREP` 추가, `SPOT`을 `SPOT_PUBSUB`으로 정렬 |
| multi | `DEALER_DEALER`, `DEALER_ROUTER_SENDSEND`, `ROUTER_ROUTER_SENDSEND`, `DEALER_ROUTER_REQREP`, `ROUTER_ROUTER_REQREP`, `ROUTER_ROUTER_ONEWAY`, `PUBSUB`, `SPOT_PUBSUB`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` | `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` | Python과 같음 | Python과 같음 | send/send, req/rep, one-way 축을 C 이름과 의미로 분리하고 `SPOT_PUBSUB` 이름 사용 |

각 lane은 C source와 해당 언어 source를 pattern별로 연결하고 transport, process 역할, ready·active phase,
handshake token, timestamp와 `RESULT` 확정 지점을 확인한다. full matrix와 성능 수치 판정은 PGR-PERF로
넘기며, 이 작업에서는 정렬 후 64-byte 전체 pattern single·multi smoke만 실행한다.
[source 단위 대응표](./pgr-00-perf-correspondence.ko.md)는 현재 축약·누락과 lane review에서 채울 의미 대조
지점을 별도로 고정한다.

## 5. package와 platform 입력

payload 디렉터리와 실제 loader 지원은 같지 않다. 현재 언어별 입력은 다음과 같다.

| 언어 | payload 디렉터리 | loader가 실제 선택하는 대상 | 현재 gap |
|---|---|---|---|
| Python | Linux·macOS·Windows의 `x86_64`·`aarch64` | Linux·macOS 두 architecture, Windows `x86_64`와 존재하지 않는 `windows-x86` | Windows `aarch64` 선택 누락, `x86` 오기 |
| Go | Linux·macOS·Windows의 `x86_64`·`aarch64` | cgo directive는 Linux·macOS 두 architecture | Windows link directive 누락 |
| Rust | Linux·macOS·Windows의 `x86_64`·`aarch64` | `build.rs`가 여섯 조합 모두 선택 | 각 runner native 실행 필요 |

최종 완료에는 계획이 선언한 여섯 조합의 native 실행 증거가 필요하다. 이 호스트에서 직접 확인할 수 없는
조합은 archive 검사만으로 완료하지 않고 실행 진행표에 runner 차단으로 남긴다. 후보 검증은 GitHub
Release가 없어도 실행할 수 있어야 하며, release workflow의 외부 게시 단계와 분리한다.

## 6. 다음 gate

- PGR-01 draft review가 끝나기 전 public API를 구현하지 않는다.
- fresh Core runtime과 ABI layout 결과가 없으면 native payload를 동기화하지 않는다.
- 세 언어는 Python, Go, Rust 순서로만 진행한다.
- 한 언어의 test, sample, package consumer, perf smoke, review와 push가 끝나기 전 다음 언어를 시작하지 않는다.
