# Node.js sample gap audit 기준과 검증 결과

이 기록은 `node-framework-spec-gap-ledger.ko.md` 11절의 최초 sample gap 판정에 사용한 working tree를
식별한다. Working tree가 clean하지 않으므로 commit만으로 같은 내용을 복원할 수 없다. 이 결과는 최초
gap을 설명하는 historical baseline이며 현재 candidate의 완료 evidence로 사용하지 않는다.

## 1. 기준 fingerprint

- 기준 시각: 2026-08-02 09:08 KST
- `HEAD`: `ac5335acb2843ab73e23b34d8afd33203b388d60`
- Audit 입력 파일 내용 SHA-256:
  `5706e8199f3918f0acb58054e08f55fb24e375aa5b429dc095db8140c057d49f`

조사 범위에는 공통 sample 7개 문서가 모두 수정된 상태였고, Node sample source에서는
`SupportChat.Ts/Server/Support/Infrastructure/ZLink/Actors/support-user-actor.ts`가 수정된 상태였다.
Node Framework production source, exact interface, E2E와 test에도 별도 변경이 있었다. Fingerprint에는
공통 sample, Node sample, sample 작성 guide, 이 ledger가 인용한 Framework spec과 Node exact interface,
Node package version과 sample contract test를 포함한다. Node Framework production source, 전체
dependency lock과 실제 package·native artifact는 포함하지 않으므로, fingerprint가 같아도 현재
candidate의 build와 process 결과를 증명하지 않는다.

검증 실행 뒤 `HEAD`가 이동했지만 audit 입력 파일의 내용은 바뀌지 않아 같은 fingerprint가 계산됐다.
이 사례는 fingerprint를 현재 실행 evidence로 재사용할 수 없다는 경계를 보여 준다.

아래 명령은 최초 sample 계약 비교 입력이 같은지 확인할 때만 사용한다. 현재 candidate를 검증할 때는
candidate SHA와 전체 변경 manifest, Node Framework production source, dependency lock, package archive와
native artifact hash를 별도로 고정하고 build, contract test와 process runner를 다시 실행한다.

```text
{
  find framework/languages/node/samples framework/doc/framework/common/sample -type f -print0
  printf '%s\0' doc/principal/documentation/sample-writing-guide.ko.md \
    framework/doc/framework/common/spec/03-interaction-model.ko.md \
    framework/doc/framework/common/spec/14-actor-model.ko.md \
    framework/doc/framework/common/spec/20-session-actor-dispatch.ko.md \
    framework/doc/framework/common/spec/server/languages/node/interfaces/02-channel-messaging.ko.md \
    framework/doc/framework/common/spec/server/languages/node/interfaces/03-location-observability.ko.md \
    framework/doc/framework/common/spec/server/languages/node/interfaces/04-spots.ko.md \
    framework/doc/framework/common/spec/server/languages/node/interfaces/05-actors.ko.md \
    framework/languages/node/package.json
  find framework/languages/node/test/contract -maxdepth 1 -type f -name 'sample*.test.js' -print0
} | sort -z | xargs -0 sha256sum | sha256sum
```

## 2. 실행 결과

### Node build

```text
cd framework/languages/node
npm run build
```

- exit code: `0`
- 결과: TypeScript build와 browser bundle build 통과
- 한계: compile 성공이며 sample message와 업무 흐름이 공통 계약과 일치한다는 증거는 아니다.

### Sample contract suite

```text
cd framework/languages/node
node --test test/contract/sample*.test.js
```

- exit code: `1`
- 전체: `83`
- 통과: `81`
- 실패: `2`
- 취소·건너뜀: `0`
- 실행 시간: `43280.72694 ms`

실패는 다음 두 항목이다.

1. `DeliveryDispatch uses public Actor APIs without internal Spot handle resolvers`
   - `sample-deliverydispatch-spot-handle-gate.test.js:34`가
     `addRouteMesh(SampleNames.routeMesh)`를 요구한다.
   - 현재 source는 `SampleNames.courierMeshName`을 사용하며 공통 sample도 courier와 customer
     RouteMesh를 구분한다. Public Actor API와 private Spot handle 금지라는 본래 검사는 유지하되
     RouteMesh 이름 assertion을 현재 계약에 맞춰야 한다.
2. `Node sample aggregate runner passes all canonical samples`
   - Bingo 실제 runner가 replacement peer의 `ConnectionReady` evidence를 기다리다 timeout으로
     종료됐다.
   - Source symbol이나 completion 문자열 검사가 아니라 실제 process smoke 실패다. 원인을 확인하고
     같은 runner를 다시 실행하기 전에는 Bingo process evidence를 통과로 판정하지 않는다.

## 3. 현재 판정

- Build: 통과
- Static·process 혼합 sample contract suite: 실패
- 일곱 sample 통합 실행: 통과하지 않음
- Sample 계약 충족: 미판정

다음 audit에서는 candidate SHA, 전체 변경 manifest와 fingerprint를 함께 기록한다. Fingerprint는 sample
계약 비교 입력의 변경만 감지한다. Candidate SHA 또는 전체 manifest가 이전 실행과 다르면 fingerprint가
같아도 build, exact inventory test와 실제 sample runner를 fresh package로 다시 실행한다.
