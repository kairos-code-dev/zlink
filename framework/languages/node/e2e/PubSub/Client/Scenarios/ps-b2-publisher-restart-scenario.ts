// PS-B2: terminal drain과 peer row 교체 뒤 기존 subscription의 첫 event 수신을 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { getStatus, postJsonWithin, postStatus } from '../../../http-client';
import { ensure, eventually, isConnectionFailure } from '../Support/scenario-assert';
import {
  evidenceCount,
  waitForConnection,
  waitForDisconnection,
  waitForEvent,
  waitForPublisherRow
} from '../Support/subscriber-observation';
import { ServerProcessLauncher, type DynamicProcess } from '../Support/server-process-launcher';
import { publishEvent } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsB2(
  publisher: string,
  subscribers: readonly string[],
  processes: ServerProcessLauncher,
  publisherEndpoint: string
): Promise<DynamicProcess> {
  const runId = randomUUID().replaceAll('-', '');
  await publishEvent(publisher, PubSubNames.mainTopic, runId, 1, 'before-publisher-restart');
  await Promise.all(subscribers.map((url) => waitForEvent(url, runId, 1, 'before-publisher-restart')));
  await waitForPublisherRow(subscribers[0], true, publisherEndpoint);
  const disconnectOffsets = await Promise.all(subscribers.map(evidenceCount));

  const drain = await postJsonWithin<{ readonly kind: string; readonly reason?: string }>(
    publisher, '/admin/drain', {}, 35_000
  );
  ensure(
    drain.kind === 'drained' && drain.reason === undefined,
    `PS-B2 expected terminal Drained, got ${drain.kind}/${drain.reason ?? ''}.`
  );
  await waitForPublisherRow(subscribers[0], false);
  await Promise.all(subscribers.map((url, index) => waitForDisconnection(url, disconnectOffsets[index])));

  await postStatus(`${publisher}/shutdown`);
  await eventually(async () => {
    try {
      return await getStatus(`${publisher}/health`) !== 200;
    } catch (error) {
      return isConnectionFailure(error);
    }
  }, 'PS-B2 expected publisher process to stop.');

  try {
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 2, 'during-publisher-down');
    throw new Error('PS-B2 expected publish attempt to fail while publisher is down.');
  } catch (error) {
    ensure(isConnectionFailure(error), 'PS-B2 expected HTTP connection failure while publisher is down.');
  }

  const connectionOffsets = await Promise.all(subscribers.map(evidenceCount));
  const restarted = processes.startPublisher();
  await restarted.waitReady();
  await waitForPublisherRow(subscribers[0], true, publisherEndpoint);
  await Promise.all(subscribers.map((url, index) => waitForConnection(url, connectionOffsets[index])));
  await publishEvent(publisher, PubSubNames.mainTopic, runId, 3, 'after-publisher-restart');
  await Promise.all(subscribers.map((url) => waitForEvent(url, runId, 3, 'after-publisher-restart')));
  console.log('scenario PS-B2 passed');
  return restarted;
}
