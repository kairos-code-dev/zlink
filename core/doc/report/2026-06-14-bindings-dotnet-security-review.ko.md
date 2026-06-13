# .NET 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/dotnet/src/Zlink/Runtime/Native`, `bindings/dotnet/src/Zlink/Runtime/Messaging`
- 검토 방식: native library 로더, 메시지 크기 변환, native payload 복사 경로를 코드 기준으로 확인했다.
- 상태: 주의 항목 2건

## 요약

.NET 바인딩은 P/Invoke로 core native library를 호출한다. 따라서 native library를 어디에서 로드하는지, native가 돌려준 크기를 managed 영역에서 어떻게 다루는지가 핵심 경계다.

검토 결과 즉시 기능을 깨는 결함은 확인되지 않았다. 다만 신뢰할 수 없는 실행 환경에서는 native library 로딩 경로가 공격면이 될 수 있고, 일부 메시지 크기 변환이 checked 변환과 일관되지 않다.

## 확인된 이슈

### DOTNET-BINDING-001: 환경 변수 기반 native library 로딩은 신뢰된 배포 환경을 전제로 한다

- 심각도: 중간
- 근거:
  - `bindings/dotnet/src/Zlink/Runtime/Native/NativeLibraryLoader.cs:72-79`는 `ZLINK_LIBRARY_PATH`가 있으면 해당 경로의 native library를 로드한다.
  - 같은 파일 `120-135`는 필수 export를 확인해 잘못된 library를 어느 정도 걸러낸다.
  - 같은 파일 `180-187`은 well-known library name fallback도 사용한다.
- 영향:
  - 환경 변수를 공격자가 제어할 수 있는 프로세스에서는 임의 native library 로딩 위험이 있다.
  - 일반적인 애플리케이션 배포에서 환경 변수를 운영자가 관리한다면 기능 문제는 아니다.
  - 성능 영향은 없다.
- 권장 조치:
  - 문서에 `ZLINK_LIBRARY_PATH`는 개발·진단용 또는 신뢰된 배포 환경에서만 사용한다고 명시한다.
  - 보안이 중요한 서비스에서는 절대 경로, 파일 소유자, 서명 또는 배포 디렉터리 고정을 검토한다.

### DOTNET-BINDING-002: 일부 메시지 크기 변환이 checked 변환과 다르다

- 심각도: 낮음
- 근거:
  - `bindings/dotnet/src/Zlink/Runtime/Messaging/Message.NativeVector.cs:24`는 `zlink_msg_size`의 `nuint` 값을 `int`로 직접 변환한다.
  - 같은 파일 `45-53`은 count와 size에 `checked((int)...)`를 사용한다.
  - `bindings/dotnet/src/Zlink/Runtime/Messaging/Message.Native.cs:55`, `93`, `110`도 native 메시지 크기를 `int`로 직접 변환한다.
  - `bindings/dotnet/src/Zlink/Runtime/Messaging/Message.Native.cs:144-164`는 destination 길이와 `nuint` size를 먼저 비교한 뒤 복사한다.
- 영향:
  - 정상 core 메시지 크기가 managed `int` 범위를 넘지 않는다는 전제가 맞으면 기능 문제는 없다.
  - core 또는 외부 native 입력이 비정상 크기를 돌려주면 일부 경로에서 overflow 처리 방식이 달라질 수 있다.
  - 성능 영향은 없다.
- 권장 조치:
  - 메시지 크기를 `int`로 바꾸는 모든 경로에 같은 정책을 적용한다. 예외를 내야 하는 경로는 `checked`로 명확히 하고, destination 크기만 확인하면 되는 경로는 `nuint` 비교 후 변환한다.

## 기능·성능 검토

- native export 검증이 있어 잘못된 library를 어느 정도 조기에 거부한다.
- payload 복사 경로는 `Span<byte>`와 `ReadOnlySpan<byte>`를 사용해 불필요한 중간 복사를 줄인다.
- 현재 확인한 범위에서는 성능 저하를 만들 만한 새 병목은 없다.

## 결론

.NET 바인딩은 일반적인 신뢰된 배포 환경에서는 큰 기능 문제 없이 동작할 구조다. 보안 관점에서는 native library 로딩 경계를 문서화하고, 메시지 크기 변환 정책을 일관되게 맞추는 편이 좋다.
