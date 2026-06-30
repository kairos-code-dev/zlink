import type { EchoRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { expectStartupFailure } from '../Support/process-support';
import { ensure } from '../Support/scenario-assert';

export async function runRcA1(serverUrl: string): Promise<void> {
  const reply = await postJson<EchoRes>(serverUrl, '/registration/auto');
  ensure(reply.value === 'echo:rc-a1', 'RC-A1 reply value mismatch.');
  ensure(reply.contentType === 'application/json', 'RC-A1 expected JSON content type.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['variant=auto', 'rc-a1-send'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('echo-request|variant=auto')), 'RC-A1 request evidence missing.');
  ensure(evidence.some((line) => line.includes('echo-command|variant=auto')), 'RC-A1 send evidence missing.');
  console.log('scenario RC-A1 passed');
}

export async function runRcA2(serverUrl: string): Promise<void> {
  const reply = await postJson<EchoRes>(serverUrl, '/registration/attribute');
  ensure(reply.value === 'echo:rc-a2', 'RC-A2 reply value mismatch.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['variant=attr', 'rc-a2-send'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('echo-request|variant=attr')), 'RC-A2 request evidence missing.');
  ensure(evidence.some((line) => line.includes('echo-command|variant=attr')), 'RC-A2 send evidence missing.');
  console.log('scenario RC-A2 passed');
}

export async function runRcA3(serverUrl: string): Promise<void> {
  const reply = await postJson<EchoRes>(serverUrl, '/registration/manual');
  ensure(reply.value === 'echo:rc-a3', 'RC-A3 reply value mismatch.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['variant=manual', 'rc-a3-send'],
    timeoutMilliseconds: 10_000
  });
  ensure(evidence.some((line) => line.includes('echo-request|variant=manual')), 'RC-A3 request evidence missing.');
  ensure(evidence.some((line) => line.includes('echo-command|variant=manual')), 'RC-A3 send evidence missing.');
  console.log('scenario RC-A3 passed');
}

export async function runRcA4(serverUrl: string): Promise<void> {
  const replies = await postJson<readonly EchoRes[]>(serverUrl, '/registration/di-filter-order');
  ensure(replies.length === 2, 'RC-A4 expected two replies.');
  ensure(replies[0]?.value === 'echo:rc-a4-1', 'RC-A4 first reply mismatch.');
  ensure(replies[1]?.value === 'echo:rc-a4-2', 'RC-A4 second reply mismatch.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: ['di|value=rc-a4-1', 'di|value=rc-a4-2'],
    timeoutMilliseconds: 10_000
  });
  const di = evidence.filter((line) => line.includes('di|'));
  const singletonIds = new Set(di.map((line) => extractValue(line, 'singleton')));
  const scopedIds = new Set(di.map((line) => extractValue(line, 'scoped')));
  ensure(di.length >= 2, 'RC-A4 DI evidence missing.');
  ensure(singletonIds.size === 1, 'RC-A4 expected stable singleton dependency.');
  ensure(scopedIds.size >= 2, 'RC-A4 expected per-dispatch scoped dependencies.');
  console.log('scenario RC-A4 passed');
}

export async function runRcA5(serverUrl: string): Promise<void> {
  const reply = await postJson<EchoRes>(serverUrl, '/registration/filter-order');
  ensure(reply.value === 'echo:rc-a5', 'RC-A5 reply value mismatch.');
  const evidence = await postJson<readonly string[]>(serverUrl, '/evidence/wait', {
    containsAll: [
      'filter|name=first|phase=before|packet=EchoManualReq',
      'filter|name=second|phase=before|packet=EchoManualReq',
      'filter|name=second|phase=after|packet=EchoManualReq',
      'filter|name=first|phase=after|packet=EchoManualReq',
      'filter-reply|value=echo:rc-a5'
    ],
    timeoutMilliseconds: 10_000
  });
  const order = evidence
    .filter((line) => line.includes('filter|') && line.includes('packet=EchoManualReq'))
    .slice(-4)
    .map((line) => {
      const name = line.includes('name=first') ? 'first' : 'second';
      const phase = line.includes('phase=before') ? 'before' : 'after';
      return `${name}:${phase}`;
    });
  ensure(
    order.join(',') === 'first:before,second:before,second:after,first:after',
    `RC-A5 filter order mismatch: ${order.join(',')}`
  );
  console.log('scenario RC-A5 passed');
}

function extractValue(line: string, key: string): string {
  const marker = `${key}=`;
  const start = line.indexOf(marker);
  if (start < 0) {
    return '';
  }
  const valueStart = start + marker.length;
  const end = line.indexOf('|', valueStart);
  return end < 0 ? line.slice(valueStart) : line.slice(valueStart, end);
}

export async function runRcA6(options: ClientOptions): Promise<void> {
  const output = await expectStartupFailure(
    options.invalidMain,
    [
      '--rid', 'invalid-duplicate',
      '--http-url', options.invalidHttpUrl,
      '--channel-endpoint', options.invalidChannelEndpoint,
      '--log-dir', options.logDir
    ],
    options.logDir,
    'invalid-duplicate'
  );
  ensure(output.includes('Duplicate') || output.includes('duplicate'), 'RC-A6 expected duplicate registration error output.');
  console.log('scenario RC-A6 passed');
}
