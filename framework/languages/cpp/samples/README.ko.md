# ZLink C++ Framework Samples

C++ 샘플은 `app_t`가 제공하는 configuration 기능을 사용한다. 실행
설정은 `app.config()`에서 JSON, 환경 변수, CLI 값을 읽고, 샘플 topology 구조체로
bind한 뒤 framework builder 에 전달한다.

서버 코드는 자기 role 하나만 구성하고 실행한다. 다른 서버나 client 를 코드 안에서
시작하지 않는다. 실행을 자동화할 때도 설정 파일을 만들고 각 role 실행 파일을 별도
process 로 시작하는 방식을 사용한다.

각 샘플의 `Shared/Contracts`에는 client 와 server 가 함께 serialize 하는 message 계약만
둔다. 서버 topology, endpoint 이름, packet 이름, host 설정은 `Server/Configuration`에
두고, client 전용 설정은 `Client/Configuration`에 둔다.

Bingo는 Protobuf payload를 사용하고, TicTacToe와 DeliveryDispatch는 framework 기본 JSON
codec을 사용한다. 샘플 코드에서 codec 차이 때문에 업무 DTO나 handler 호출 모양을 바꾸지 않는다.

## 실행

CMake sample smoke 는 각 role 실행 파일을 빌드하고 기본 실행 경로를 확인한다.
TicTacToe sample-local script 는 Play 서버와 API 서버를 별도 process로 계속 실행한 뒤
public client 실행 파일로 full client/server self-check 를 수행한다. Bingo sample-local script 는
Registry, API, Play, Session 서버를 별도 process로 계속 실행한 뒤 public client 실행 파일로
full client/server self-check 를 수행한다.

Linux 또는 WSL:

```bash
./framework/languages/cpp/samples/TicTacToe/run_sample.sh
./framework/languages/cpp/samples/Bingo/run_sample.sh
./framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh
./framework/languages/cpp/samples/run_samples.sh
```

Windows PowerShell:

```powershell
.\framework\languages\cpp\samples\TicTacToe\run_sample.ps1
.\framework\languages\cpp\samples\Bingo\run_sample.ps1
.\framework\languages\cpp\samples\run_samples.ps1
```

DeliveryDispatch 샘플은 현재 Linux 또는 WSL용 `run_sample.sh` 경로로 검증한다.

기본 빌드 디렉토리는 `framework/languages/cpp/build`이다. 다른 디렉토리에 빌드했다면
`ZLINK_CPP_BUILD_DIR` 환경 변수로 실행 파일 위치를 넘긴다.

## Configuration

수동 서버 실행에서는 `--config`로 JSON 설정 파일을 넘긴다. 서버 role 을 계속 실행하려면
설정 파일에서 `sample.host.keepRunning` 값을 `true`로 둔다. Client 실행 파일은 framework
host가 아니므로 `app.config()`를 쓰지 않는다. Client는 stream connector 또는 HTTP client가
접속할 endpoint만 CLI 인자나 client 전용 환경 변수로 받는다.

```bash
sample_cpp_framework_tictactoe_play --config=./appsettings.json
sample_cpp_framework_tictactoe_api --config=./appsettings.json
sample_cpp_framework_tictactoe_client --api-http-endpoint=http://127.0.0.1:48113
```

서버 설정 파일은 `sample.topology` 아래 endpoint 값을 둔다.

```json
{
  "sample": {
    "host": {
      "keepRunning": "true"
    },
    "topology": {
      "apiEndpoint": "tcp://127.0.0.1:48103",
      "apiHttpEndpoint": "http://127.0.0.1:48113",
      "playEndpoint": "tcp://127.0.0.1:48104",
      "streamEndpoint": "tcp://127.0.0.1:48112"
    }
  }
}
```

서버 환경 변수 override 는 `ZLINK_CPP_SAMPLE__` prefix 와 `__` 구분자를 사용한다.

```bash
ZLINK_CPP_SAMPLE__sample__topology__apiEndpoint=tcp://127.0.0.1:50001 \
  sample_cpp_framework_tictactoe_api --config=./appsettings.json
```

Client 전용 환경 변수는 framework configuration 계층을 거치지 않는다.

```bash
ZLINK_CPP_CLIENT_API_HTTP_ENDPOINT=http://127.0.0.1:48113 sample_cpp_framework_tictactoe_client
ZLINK_CPP_CLIENT_STREAM_ENDPOINT=tcp://127.0.0.1:47114 sample_cpp_framework_bingo_client
```
