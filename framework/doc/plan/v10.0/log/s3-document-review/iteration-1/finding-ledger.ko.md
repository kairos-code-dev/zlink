# S3 문서 finding — iteration 1

## Finding

| ID | 축 | 심각도 | 문제 | 근거 | 처리 |
|---|---|---|---|---|---|
| S3-I1-ROOT-01 | 1차 소스 | blocker | 언어별 sample 루트 안내 4개가 S2 영향 inventory와 S3 동결 범위에서 빠졌다 | 실제 sample root에 C++, .NET, Java/Kotlin, Node.js 공개 안내가 존재하지만 `scope-files.txt` 217개에는 포함되지 않았다 | S2 inventory §12.1에 4개를 추가했다. 문서 내용을 정식 계약에 맞춘 뒤 새 iteration의 범위와 hash에 포함한다 |

누락 문서:

- `framework/languages/cpp/samples/README.ko.md`
- `framework/languages/dotnet/samples/README.md`
- `framework/languages/java/samples/README.md`
- `framework/languages/node/samples/README.ko.md`

이 finding은 iteration 1의 범위 자체를 무효화한다. reviewer 결과와 관계없이 같은 범위를 clean으로
승인할 수 없으며, 네 문서를 포함한 전체 문서 집합을 새 iteration에서 두 reviewer가 다시 검토해야 한다.
