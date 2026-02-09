# buildenv — zlink 개발환경 설정

WSL Ubuntu 24.04에서 zlink 프로젝트의 전체 개발환경을 자동으로 구축하는 스크립트 모음.

## 빠른 시작

```bash
# 전체 환경 설치 (Core + 모든 바인딩)
./buildenv/setup.sh

# 설치 후 확인
./buildenv/check.sh
```

## 스크립트 구성

| 파일 | 역할 |
|------|------|
| `setup.sh` | 개발환경 설치 (메인 스크립트) |
| `check.sh` | 설치 상태 확인 (설치 없이 검증만) |
| `_common.sh` | 공통 유틸리티 (직접 실행하지 않음) |

## setup.sh 사용법

```bash
./buildenv/setup.sh [OPTIONS]
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
./buildenv/setup.sh --core --node

# Python + Java만 설치
./buildenv/setup.sh --python --java

# 현재 상태 확인만
./buildenv/setup.sh --check
```

## 설치 항목 상세

### 시스템 기본 도구 (항상 설치)

build-essential, cmake, pkg-config, git, curl, wget, unzip,
autoconf, automake, libtool, lsb-release, ninja-build, ccache

### Core C/C++ (`--core`)

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

### Node.js (`--node`)

| 패키지 | 용도 |
|--------|------|
| Node.js >= 20 | 런타임 (NodeSource 저장소) |
| node-gyp | 네이티브 애드온 빌드 |
| python-is-python3 | binding.gyp 호환 |

### Python (`--python`)

| 패키지 | 용도 |
|--------|------|
| python3, python3-dev | 런타임 + 헤더 |
| python3-pip, python3-venv | 패키지 관리 |
| setuptools >= 68, wheel | 빌드 도구 |

### Java (`--java`)

| 패키지 | 용도 |
|--------|------|
| JDK 22 | Eclipse Adoptium Temurin |
| Gradle 8.7 | 빌드 도구 (/opt/gradle에 설치) |

### .NET (`--dotnet`)

| 패키지 | 용도 |
|--------|------|
| dotnet-sdk-8.0 | .NET 8.0 SDK (Ubuntu 네이티브 패키지) |

## check.sh 사용법

```bash
./buildenv/check.sh
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
- **sudo 필요**: 시스템 패키지 설치를 위해 sudo 권한 필요
- **대상 플랫폼**: WSL Ubuntu 24.04 (다른 배포판은 수동 조정 필요)
- **버전 상수**: `_common.sh` 상단에서 모든 요구 버전을 관리
