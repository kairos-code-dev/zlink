# .NET 공개 계약 산출물 부속 명세

이 문서는 사람이 읽는 `.NET` 공개 계약과 실제 배포 산출물을 정확히 비교하는 방법을 정의한다.
공개 API의 의미와 사용 조건은 이 디렉토리의 기능별 문서가 설명한다. 아래 text snapshot은 그 계약을
assembly와 NuGet package에서 한 항목도 빠뜨리지 않고 검사하기 위한 기계 판독 가능한 부속 명세다.

## 1. API snapshot

`public-contract/api/`에는 정식 배포 대상 assembly 여섯 개의 모든 public type과 member를 기록한다.
각 파일은 다음 항목을 구분한다.

- assembly와 type 소유권
- base type과 구현 interface
- field, constructor, property, event와 method
- overload, parameter 이름, 기본값과 `ref`·`in`·`out`
- nullable read/write 상태
- generic variance와 runtime metadata에 남는 제약
- `required`, `init`, readonly struct, ref struct와 custom modifier

검증기는 정식 snapshot, source build assembly, 실제 NuGet package assembly를 세 방향으로 비교한다.
세 결과 중 하나라도 다르면 공개 계약 검증은 실패한다.

## 2. package snapshot

`public-contract/packages/`에는 package마다 다음 정보를 기록한다.

- 전체 archive entry 경로
- package id, 작성자, 설명과 repository metadata
- target framework별 dependency id, version과 asset 제외 조건
- 배포 DLL과 XML 문서의 위치

매번 달라지는 contract 검증용 version, Git commit 값과 NuGet core-properties 파일 이름만 명시한
placeholder로 정규화한다. 그 밖의 예상하지 않은 파일이나 metadata 변화는 검증 실패다.

## 3. 갱신 절차

일반 검증은 다음 명령을 사용한다.

```bash
cd framework/languages/dotnet
./scripts/verify_packaged_contract.sh
```

공개 계약 변경 후보를 검토할 때는 정식 spec 디렉토리 밖의 별도 경로에 snapshot 후보를 생성한다.

```bash
./scripts/verify_packaged_contract.sh \
  --generate-snapshot /tmp/zlink-dotnet-contract-review
```

생성 모드는 정식 spec 디렉토리를 직접 덮어쓰지 못한다. 후보와 현재 snapshot의 diff를 기능별 공개
계약 문서의 전·후 시그니처와 함께 리뷰한 뒤, 승인된 항목만 정식 snapshot에 반영한다. 구현이
달라졌다는 이유만으로 snapshot을 먼저 갱신하지 않는다. 검증기는 모든 project의 MSBuild
`IsPackable` 평가 결과, 정확한 6개 package 집합, package source mapping, 임시 package cache와
깨끗한 consumer 실행까지 함께 확인한다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | 정식 API snapshot과 source assembly의 모든 공개 서명이 일치한다. |
| `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions` | snapshot renderer가 nullable, accessor, generic, by-ref와 type 제약을 서로 다른 계약으로 기록한다. |
