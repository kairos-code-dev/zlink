// PS-B2: publisher 재시작 시나리오를 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { getStatus, postJson, postStatus } from '../../../http-client';
import { ServerProcessLauncher, type DynamicProcess } from '../Support/server-process-launcher';
import { delay, ensure, eventually, isConnectionFailure } from '../Support/scenario-assert';
import { publishEvent, waitForAll } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsB2(
  publisher: string,
  subscribers: readonly string[],
  processes: ServerProcessLauncher
): Promise<DynamicProcess> {
  const runId = randomUUID().replaceAll('-', '');
  await publishEvent(publisher, PubSubNames.mainTopic, runId, 1, 'before-publisher-restart');
  await waitForAll(subscribers, {
    containsAll: ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`, 'seq=1'],
    timeoutMilliseconds: 10_000
  });

  await postStatus(`${publisher}/shutdown`);
  await eventually(async () => {
    try {
      const status = await getStatus(`${publisher}/health`);
      return status < 200 || status >= 300;
    } catch (error) {
      return isConnectionFailure(error);
    }
  }, 'PS-B2 expected publisher to stop before restart.');

  try {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 2, 'during-publisher-down');
    throw new Error('PS-B2 expected publish attempt to fail while publisher is down.');
  } catch (error) {
    ensure(isConnectionFailure(error), 'PS-B2 expected connection failure while publisher is down.');
  }

  const restarted = processes.startPublisher();
  await restarted.waitReady();
  await delay(500);
  for (let i = 3; i <= 42; i += 1) {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, i, `after-publisher-restart-${i}`);
    await delay(100);
  }

  await waitForAll(subscribers, {
    containsAll: ['event|', `run=${runId}`, `topic=${PubSubNames.mainTopic}`],
    containsAnyLineGroups: Array.from({ length: 23 }, (_, index) => {
      const seq = index + 20;
      return [`seq=${seq}|`, `run=${runId}`, `topic=${PubSubNames.mainTopic}`];
    }),
    timeoutMilliseconds: 10_000
  });
  console.log('scenario PS-B2 passed');
  return restarted;
}
