// PS-A4: subscriber 재연결·재구독 시나리오를 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ServerProcessLauncher } from '../Support/server-process-launcher';
import { delay, ensure, eventually, isConnectionFailure } from '../Support/scenario-assert';
import { publishEvent, waitForAll } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsA4(
  publisher: string,
  reconnectSubscriberUrl: string,
  fastSubscribers: readonly string[],
  processes: ServerProcessLauncher
): Promise<void> {
  const runId = randomUUID().replaceAll('-', '');
  const subscriber = processes.startSubscriber('sub-reconnect', reconnectSubscriberUrl, 'sub-reconnect.evidence.log');
  await subscriber.waitReady();
  for (let i = 1; i <= 20; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 1, 'before-disconnect');
    await delay(25);
  }
  await waitForSubscriber(reconnectSubscriberUrl, runId);
  await subscriber.kill();

  await eventually(async () => {
    try {
      const response = await fetch(`${reconnectSubscriberUrl}/health`);
      return !response.ok;
    } catch (error) {
      return isConnectionFailure(error);
    }
  }, 'PS-A4 expected reconnect subscriber to leave before gap publish.');

  for (let i = 2; i <= 4; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, i, `gap-${i}`);
  }
  await waitForAll(fastSubscribers, {
    containsAllLineGroups: [
      ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`, 'seq=2|', 'value=gap-2'],
      ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`, 'seq=3|', 'value=gap-3'],
      ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`, 'seq=4|', 'value=gap-4']
    ],
    timeoutMilliseconds: 10_000
  });

  const restarted = processes.startSubscriber('sub-reconnect', reconnectSubscriberUrl, 'sub-reconnect.evidence.log');
  try {
    await restarted.waitReady();
    for (let i = 5; i <= 28; i += 1) {
      await publishEvent(publisher, PubSubNames.mainTopic, runId, i, `after-reconnect-${i}`);
      if (i % 5 === 0) {
        await delay(50);
      }
    }
    const evidence = await waitForSubscriber(reconnectSubscriberUrl, runId);
    ensure(
      evidence.every((line) => !line.includes(`run=${runId}`) || !line.includes('value=gap-')),
      'PS-A4 reconnected subscriber replayed disconnect-gap events.'
    );
    console.log('scenario PS-A4 passed');
  } finally {
    await restarted.kill();
  }
}

async function waitForSubscriber(subscriber: string, runId: string): Promise<readonly string[]> {
  return await postJson<readonly string[]>(subscriber, '/evidence/wait', {
    containsAll: ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`],
    timeoutMilliseconds: 10_000
  });
}
