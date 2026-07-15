// PS-A1: fanout basic delivery 시나리오를 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames, type EvidenceWaitReq } from '../../Shared/messages';
import { commonContiguousSequence } from '../Support/evidence';
import { postJson } from '../Support/http-client';
import { delay, ensure } from '../Support/scenario-assert';

export async function runPsA1(publisher: string, subscribers: readonly string[]): Promise<void> {
  const runId = randomUUID().replaceAll('-', '');
  for (let i = 1; i <= 120; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, i, `warmup-${i}`);
    if (i % 10 === 0) {
      await delay(25);
    }
  }
  await waitForAll(subscribers, { containsAll: ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`], timeoutMilliseconds: 10_000 });

  const measureStart = 1000;
  const measureCount = 12;
  for (let i = measureStart; i < measureStart + measureCount; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, i, `measure-${i}`);
  }
  const snapshots = await waitForAll(subscribers, {
    containsAll: ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`],
    containsAllLineGroups: [0, 1, 2].map((offset) => [
      `seq=${measureStart + offset}|`,
      `value=measure-${measureStart + offset}`
    ]),
    timeoutMilliseconds: 10_000
  });
  ensure(
    commonContiguousSequence(
      snapshots,
      runId,
      PubSubNames.mainTopic,
      measureStart,
      measureStart + measureCount - 1,
      'measure-'
    ).length >= 3,
    'PS-A1 expected common contiguous sequence on all subscribers.'
  );
  console.log('scenario PS-A1 passed');
}

export async function publishEvent(publisher: string, topic: string, runId: string, sequence: number, value: string): Promise<void> {
  await postJson(publisher, '/publish/event', { topic, runId, sequence, value });
}

export async function waitForAll(subscribers: readonly string[], request: EvidenceWaitReq): Promise<readonly (readonly string[])[]> {
  return await Promise.all(subscribers.map((subscriber) => postJson<readonly string[]>(subscriber, '/evidence/wait', request)));
}
