# Java RegistrationCodec E2E

이 E2E는 공통 Config 4 등록·codec 변주 시나리오를 Java framework public API로 검증한다.

역할은 `.NET` E2E와 같은 의미로 나뉜다.

| 위치 | 역할 |
|------|------|
| `Shared/` | channel 이름, packet DTO, evidence DTO를 공유한다. |
| `Client/` | RC-A1~RC-B4 scenario를 실행한다. |
| `Server/Main/` | 등록 방식, DI lifecycle, filter ordering, JSON/Protobuf/MessagePack codec coexistence를 제공한다. |
| `Server/InvalidDuplicate/` | duplicate packet registration startup failure를 검증하기 위한 실패 전용 role이다. |
| `Server/JsonOnlyPeer/` | RC-B5 codec mismatch에서 JSON만 등록한 peer를 제공한다. |
| `Server/CodecRequester/` | RC-B5 mismatch request와 recovery check를 실행한다. |

실행은 아래 명령을 사용한다.

```bash
timeout 420s ./run_e2e.sh
```

`run_e2e.sh`는 Gradle `installDist`를 실행한 뒤 role별 binary를 띄운다. 실패하면
`logs/<run-id>/` 아래 stdout, stderr, message flow log를 출력한다.

현재 남은 gap은 codec별 exact content-type 검증이다. Java public API로 reply content-type을 직접
볼 수 없고 handler context에도 이 E2E 경로에서 codec별 content-type이 안정적으로 노출되지 않아,
raw frame이나 private runtime 접근 없이 후속 public contract parity 작업으로 남긴다.
