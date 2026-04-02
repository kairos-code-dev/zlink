# 바인딩 API 레퍼런스

각 바인딩은 해당 언어의 표준 문서 도구로 API 레퍼런스를 생성한다.

| Binding | 문서 도구 | 생성 명령 | 출력 위치 |
|---------|-----------|----------|-----------|
| C++ | Doxygen | `cd bindings/cpp && doxygen Doxyfile` | `bindings/cpp/doxygen/html/` |
| Java | Javadoc | `cd bindings/java && ./gradlew javadoc` | `bindings/java/build/docs/javadoc/` |
| Python | Sphinx | `cd bindings/python && sphinx-build -b html docs docs/_build/html` | `bindings/python/docs/_build/html/` |
| Node | TypeDoc | `cd bindings/node && npx typedoc` | `bindings/node/typedoc/html/` |
| .NET | DocFX | `cd bindings/dotnet && docfx docfx.json` | `bindings/dotnet/_site/` |
| Go | godoc | `cd bindings/go && go doc ./...` | (동적 서버) |
| Rust | rustdoc | `cd bindings/rust && cargo doc --no-deps` | `bindings/rust/target/doc/zlink/` |

## 바로가기

- [C++ Doxygen README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/README.doxygen.md)
- [Java Javadoc README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/README.javadoc.md)
- [Python Sphinx README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/README.sphinx.md)
- [Node TypeDoc README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/README.typedoc.md)
- [.NET DocFX README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/README.docfx.md)
- [Go godoc README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/README.godoc.md)
- [Rust rustdoc README](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/README.rustdoc.md)
