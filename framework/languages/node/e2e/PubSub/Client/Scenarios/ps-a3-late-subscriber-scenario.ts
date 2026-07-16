// PS-A3: ConnectionReady 전 event를 replay하지 않고 준비 뒤 첫 event를 받는지 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { getJson } from '../../../http-client';
import { NetworkFaultProxy } from '../Support/network-fault-proxy';
import { ensure } from '../Support/scenario-assert';
import { evidenceCount, waitForConnection, waitForEvent } from '../Support/subscriber-observation';
import { ServerProcessLauncher } from '../Support/server-process-launcher';
import { publishEvent } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsA3(
  publisher: string,
  lateSubscriberUrl: string,
  processes: ServerProcessLauncher,
  publisherEndpoint: string
): Promise<void> {
  const beforeReadyRun = randomUUID().replaceAll('-', '');
  await publishEvent(publisher, PubSubNames.mainTopic, beforeReadyRun, 1, 'before-ready');

  const fault = await NetworkFaultProxy.start(publisherEndpoint, true);
  const subscriber = processes.startSubscriber(
    'sub-late', lateSubscriberUrl, 'sub-late.evidence.log', fault.endpoint
  );
  try {
    await subscriber.waitReady();
    const connectionOffset = await evidenceCount(lateSubscriberUrl);
    fault.unblock();
    await waitForConnection(lateSubscriberUrl, connectionOffset);

    const afterReadyRun = randomUUID().replaceAll('-', '');
    await publishEvent(publisher, PubSubNames.mainTopic, afterReadyRun, 2, 'after-ready');
    const evidence = await waitForEvent(lateSubscriberUrl, afterReadyRun, 2, 'after-ready');
    const allEvidence = await getJson<readonly string[]>(lateSubscriberUrl, '/evidence');
    ensure(
      evidence.some((line) => line.includes(`run=${afterReadyRun}`))
        && allEvidence.every((line) => !line.includes(`run=${beforeReadyRun}`)),
      'PS-A3 late subscriber replayed before-ready or missed the first after-ready event.'
    );
    console.log('scenario PS-A3 passed');
  } finally {
    await subscriber.kill();
    await fault.close();
  }
}
