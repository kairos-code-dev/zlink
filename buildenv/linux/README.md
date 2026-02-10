# buildenv/linux — zlink Linux 개발환경 설정

WSL Ubuntu 24.04에서 zlink 프로젝트의 개발환경을 자동으로 구축하는 스크립트 모음.

두 단계로 나뉜다:

1. **개발환경 설치** (`setup.sh`) — 컴파일러, 런타임, SDK 등 시스템 도구 설치 (sudo 필요)
2. **IDE 설정** (`setup-vscode.sh`) — 설치된 도구 경로를 탐지하여 VS Code 설정 파일 생성 (sudo 불필요)

## 빠른 시작

```bash
# 1. 개발환경 설치 (sudo 필요)
./buildenv/linux/setup.sh

# 2. 설치 확인
./buildenv/linux/check.sh

# 3. VS Code 설정 생성 (sudo 불필요)
./buildenv/linux/setup-vscode.sh
```

## 스크립트 구성

| 파일 | 역할 | sudo |
|------|------|------|
| `setup.sh` | 개발환경 설치 (컴파일러, 런타임, SDK) | 필요 |
| `setup-vscode.sh` | VS Code 설정 생성 (IntelliSense, 태스크) | 불필요 |
| `check.sh` | 설치 상태 확인 (설치 없이 검증만) | 불필요 |
| `_common.sh` | 공통 유틸리티 (직접 실행하지 않음) | — |

## setup.sh — 개발환경 설치

시스템 도구와 언어별 SDK를 설치한다. sudo 권한이 필요하다.

```bash
./buildenv/linux/setup.sh [OPTIONS]
```

### 옵션

| 옵션 | 설명 |
|------|------|
| (없음), `--all` | 전체 설치 (기본값) |
| `--core` | Core C/C++ 빌드 도구만 |
| `--node` | Node.js 바인딩 환경만 |
| `--python` | Python 바인딩 환경만 |
| `--java` | Java 바인딩 환경만 |
| `--dotnet` | .NET 바인딩 환경만 |
| `--check` | 현재 설치 상태만 확인 (설치 없이) |
| `--help` | 도움말 |

### 조합 사용 예시

```bash
# Core + Node.js만 설치
./buildenv/linux/setup.sh --core --node

# Python + Java만 설치
./buildenv/linux/setup.sh --python --java

# 현재 상태 확인만
./buildenv/linux/setup.sh --check
```

### 설치 항목 상세

#### 시스템 기본 도구 (항상 설치)

build-essential, cmake, pkg-config, git, curl, wget, unzip,
autoconf, automake, libtool, lsb-release, ninja-build, ccache

#### Core C/C++ (`--core`)

| 패키지 | 용도 |
|--------|------|
| g++ | C++17 컴파일러 |
| libssl-dev | OpenSSL (TLS/WSS 전송 계층) |
| libbsd-dev | BSD 호환 함수 |
| clang-format | 코드 스타일 자동 포맷 |
| clang-tidy | 정적 분석 |
| gdb | 디버거 |
| valgrind | 메모리 분석 |
| doxygen | API 문서 생성 |

#### Node.js (`--node`)

| 패키지 | 용도 |
|--------|------|
| Node.js >= 20 | 런타임 (NodeSource 저장소) |
| node-gyp | 네이티브 애드온 빌드 |
| python-is-python3 | binding.gyp 호환 |

#### Python (`--python`)

| 패키지 | 용도 |
|--------|------|
| python3, python3-dev | 런타임 + 헤더 |
| python3-pip, python3-venv | 패키지 관리 |
| setuptools >= 68, wheel, pytest | 빌드/테스트 도구 |

#### Java (`--java`)

| 패키지 | 용도 |
|--------|------|
| JDK 22 | Eclipse Adoptium Temurin |
| Gradle 9.3.0 | 빌드 도구 (/opt/gradle에 설치) |

Java 바인딩은 `bindings/java/gradlew`(Wrapper)를 우선 사용한다.
예: `cd bindings/java && ./gradlew test`

#### .NET (`--dotnet`)

| 패키지 | 용도 |
|--------|------|
| dotnet-sdk-8.0 | .NET 8.0 SDK (Ubuntu 네이티브 패키지) |

## setup-vscode.sh — VS Code 설정 생성

`setup.sh`로 설치된 도구의 경로를 자동 탐지하여 `.vscode/` 설정 파일을 생성한다.
sudo 불필요. 도구 재설치나 경로 변경 시 다시 실행하면 된다.

`.vscode/`는 `.gitignore`에 포함되어 있으므로 각 개발자가 자신의 환경에서 실행한다.

```bash
./buildenv/linux/setup-vscode.sh [OPTIONS]
```

### 옵션

| 옵션 | 설명 |
|------|------|
| (없음) | 설정 파일 생성 (기존 파일 있으면 확인) |
| `--force` | 기존 파일 덮어쓰기 |
| `--dry-run` | 파일 생성 없이 내용만 출력 |

### 생성 파일

| 파일 | 내용 |
|------|------|
| `.vscode/settings.json` | CMake, C++ IntelliSense, Java, Python 설정 |
| `.vscode/c_cpp_properties.json` | C/C++ IntelliSense 상세 설정 |
| `.vscode/extensions.json` | 추천 확장 목록 |
| `.vscode/tasks.json` | 바인딩별 테스트 태스크 |

### 탐지 항목

| 도구 | 탐지 방법 | 미설치 시 |
|------|----------|----------|
| g++ | `command -v g++` | fallback 경로 사용 |
| Java JDK | `readlink -f $(which java)` | Java 설정 생략 |
| Gradle | `bindings/java/gradlew` 우선 | 시스템 gradle 사용 |
| .NET SDK | `readlink -f $(which dotnet)` | .NET 설정 생략 |

## check.sh — 설치 확인

```bash
./buildenv/linux/check.sh
```

28개 항목의 설치 여부와 버전 충족 여부를 테이블 형식으로 출력한다.

출력 예시:
```
  System Base Tools
  -----------------------------------------------
  cmake           3.28.3                >= 3.10
  g++             13.3.0                >= 13
  ...

  ===============================================
  Total: 28  |  Pass: 28  |  Fail: 0  |  Warn: 0
  ===============================================
```

- **Pass**: 설치 완료 + 최소 버전 충족
- **Fail**: 미설치 또는 버전 부족
- **Warn**: 설치되었으나 버전 확인 불가

## 참고 사항

- **멱등성**: 이미 설치된 패키지와 충분한 버전의 도구는 자동 스킵
- **대상 플랫폼**: WSL Ubuntu 24.04 (다른 배포판은 수동 조정 필요)
- **버전 상수**: `_common.sh` 상단에서 모든 요구 버전을 관리
