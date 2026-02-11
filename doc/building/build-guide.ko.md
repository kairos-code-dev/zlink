# 빌드 가이드

## 1. 요구 사항

- **CMake** 3.10+
- **C++17** 컴파일러 (GCC 7+, Clang 5+, MSVC 2017+)
- **OpenSSL** (TLS/WSS 지원)

## 2. 플랫폼별 빌드

### 2.1 Linux

```bash
./core/builds/linux/build.sh x64 ON
```

ARM64:
```bash
./core/builds/linux/build.sh arm64 ON
```

### 2.2 macOS

```bash
./core/builds/macos/build.sh arm64 ON
```

x64:
```bash
./core/builds/macos/build.sh x64 ON
```

### 2.3 Windows (PowerShell)

```powershell
.\core\builds\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

## 3. CMake 직접 빌드

```bash
cmake -B build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 4. 빠른 빌드 (테스트 없이)

```bash
cmake -B build -DBUILD_TESTS=OFF && cmake --build build
```

## 5. 코어 빌드 스크립트

```bash
./core/build.sh
```

Clean CMake build with tests 실행.

## 6. OpenSSL 설치

### Ubuntu/Debian
```bash
sudo apt-get install libssl-dev
```

### macOS
```bash
brew install openssl@3
```

### Windows (vcpkg)
```bash
vcpkg install openssl:x64-windows
```

## 7. CMake 옵션

상세 옵션은 [cmake-options.md](cmake-options.md)를 참고.
