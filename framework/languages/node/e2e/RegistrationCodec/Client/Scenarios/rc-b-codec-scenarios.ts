import type { CodecScenarioRes, EchoRes } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRcB1(serverUrl: string): Promise<void> {
  const reply = await postJson<EchoRes>(serverUrl, '/codec/json');
  ensure(reply.value === 'echo:rc-b1', 'RC-B1 reply value mismatch.');
  ensure(reply.contentType === 'application/json', 'RC-B1 expected application/json content type.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['codec=json', 'rc-b1-send', 'codec-reply|codec=json'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('codec-request|codec=json')), 'RC-B1 request evidence missing.');
  ensure(evidence.some((line) => line.includes('codec-command|codec=json')), 'RC-B1 command evidence missing.');
  console.log('scenario RC-B1 passed');
}

export async function runRcB2(protobufUrl: string): Promise<void> {
  const reply = await postJson<CodecScenarioRes>(protobufUrl, '/codec/protobuf');
  ensure(reply.value === 'echo:rc-b2', 'RC-B2 reply value mismatch.');
  ensure(reply.contentType === 'application/x-protobuf', 'RC-B2 expected Protobuf content type.');
  const evidence = await postJson<readonly string[]>(protobufUrl, '/evidence/wait', {
    containsAll: ['codec=protobuf', 'rc-b2-send', 'codec-reply|codec=protobuf'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('codec-request|codec=protobuf')), 'RC-B2 request evidence missing.');
  ensure(evidence.some((line) => line.includes('codec-command|codec=protobuf')), 'RC-B2 command evidence missing.');
  console.log('scenario RC-B2 passed');
}

export async function runRcB3(messagePackUrl: string): Promise<void> {
  const reply = await postJson<CodecScenarioRes>(messagePackUrl, '/codec/msgpack');
  ensure(reply.value === 'echo:rc-b3', 'RC-B3 reply value mismatch.');
  ensure(reply.contentType === 'application/x-msgpack', 'RC-B3 expected MessagePack content type.');
  const evidence = await postJson<readonly string[]>(messagePackUrl, '/evidence/wait', {
    containsAll: ['codec=msgpack', 'rc-b3-send', 'codec-reply|codec=msgpack'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('codec-request|codec=msgpack')), 'RC-B3 request evidence missing.');
  ensure(evidence.some((line) => line.includes('codec-command|codec=msgpack')), 'RC-B3 command evidence missing.');
  console.log('scenario RC-B3 passed');
}

export async function runRcB4(serverUrl: string): Promise<void> {
  const result = await postJson<CodecScenarioRes>(serverUrl, '/codec/roundtrip');
  ensure(result.json?.value === 'echo:rc-b1', 'RC-B4 JSON reply mismatch.');
  ensure(result.protobufValue?.includes('echo:rc-b2') === true, 'RC-B4 Protobuf reply mismatch.');
  ensure(result.protobufValue?.includes('content:application/x-protobuf') === true, 'RC-B4 Protobuf content type mismatch.');
  ensure(result.messagePackValue?.includes('echo:rc-b3') === true, 'RC-B4 MessagePack reply mismatch.');
  ensure(result.messagePackValue?.includes('content:application/x-msgpack') === true, 'RC-B4 MessagePack content type mismatch.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['codec-request|codec=json', 'codec-request|codec=protobuf', 'codec-request|codec=msgpack'],
    timeoutMilliseconds: 10_000
  });
  ensure(
    evidence.some((line) => line.includes('codec-request|codec=json') && line.includes('content=application/json')),
    'RC-B4 JSON evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('codec-request|codec=protobuf') && line.includes('content=application/x-protobuf')),
    'RC-B4 Protobuf evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('codec-request|codec=msgpack') && line.includes('content=application/x-msgpack')),
    'RC-B4 MessagePack evidence missing.'
  );
  console.log('scenario RC-B4 passed');
}

export async function runRcB5(codecRequesterUrl: string, jsonOnlyUrl: string): Promise<void> {
  const mismatch = await postJson<{ readonly rejected: boolean; readonly failureType?: string }>(codecRequesterUrl, '/codec/mismatch');
  ensure(mismatch.rejected, 'RC-B5 expected Protobuf request to JSON-only peer to be rejected.');
  const recovery = await postJson<EchoRes>(jsonOnlyUrl, '/codec/json-recovery');
  ensure(recovery.value === 'echo:rc-b5-json', 'RC-B5 JSON recovery reply mismatch.');
  ensure(recovery.contentType === 'application/json', 'RC-B5 JSON recovery expected JSON content type.');
  const evidence = await postJson<readonly string[]>(jsonOnlyUrl, '/evidence/wait', {
    containsAll: ['codec-mismatch-json|value=rc-b5-json', 'codec-mismatch-json-recovery|status=ok'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('codec-mismatch-json|value=rc-b5-json')), 'RC-B5 JSON recovery evidence missing.');
  console.log('scenario RC-B5 passed');
}
