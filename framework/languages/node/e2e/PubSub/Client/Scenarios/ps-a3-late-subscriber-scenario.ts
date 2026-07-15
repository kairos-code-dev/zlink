// PS-A3: late subscriber 시나리오를 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ServerProcessLauncher } from '../Support/server-process-launcher';
import { delay, ensure } from '../Support/scenario-assert';
import { publishEvent } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsA3(
  publisher: string,
  lateSubscriberUrl: string,
  processes: ServerProcessLauncher
): Promise<void> {
  const beforeLateRun = randomUUID().replaceAll('-', '');
  for (let i = 1; i <= 5; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, beforeLateRun, i, `before-late-${i}`);
  }

  const lateSubscriber = processes.startSubscriber('sub-late', lateSubscriberUrl, 'sub-late.evidence.log');
  try {
    await lateSubscriber.waitReady();
    const afterLateRun = randomUUID().replaceAll('-', '');
    for (let i = 1; i <= 40; i += 1) {
      await publishEvent(publisher, PubSubNames.mainTopic, afterLateRun, i, `after-late-${i}`);
      if (i % 5 === 0) {
        await delay(50);
      }
    }
    const evidence = await postJson<readonly string[]>(lateSubscriberUrl, '/evidence/wait', {
      containsAll: ['event|', `run=${afterLateRun}`, `topic=${PubSubNames.mainTopic}`],
      timeoutMilliseconds: 10_000
    });
    ensure(evidence.every((line) => !line.includes(`run=${beforeLateRun}`)), 'PS-A3 late subscriber replayed pre-join events.');
    console.log('scenario PS-A3 passed');
  } finally {
    await lateSubscriber.kill();
  }
}
