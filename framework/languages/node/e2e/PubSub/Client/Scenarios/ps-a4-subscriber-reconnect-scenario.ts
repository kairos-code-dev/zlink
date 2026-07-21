// PS-A4: subscriber 재연결·기존 subscription 재적용 시나리오를 검증한다.
import { randomUUID } from 'node:crypto';
import { PubSubNames } from '../../Shared/messages';
import { getJson } from '../../../http-client';
import { NetworkFaultProxy } from '../Support/network-fault-proxy';
import { ensure } from '../Support/scenario-assert';
import {
  evidenceCount,
  waitForConnection,
  waitForDisconnection,
  waitForEvent
} from '../Support/subscriber-observation';
import { ServerProcessLauncher } from '../Support/server-process-launcher';
import { publishEvent } from './ps-a1-fanout-basic-delivery-scenario';

export async function runPsA4(
  publisher: string,
  reconnectSubscriberUrl: string,
  fastSubscribers: readonly string[],
  processes: ServerProcessLauncher,
  publisherEndpoint: string
): Promise<void> {
  const runId = randomUUID().replaceAll('-', '');
  const fault = await NetworkFaultProxy.start(publisherEndpoint, true);
  const subscriber = processes.startSubscriber(
    'sub-reconnect', reconnectSubscriberUrl, 'sub-reconnect.evidence.log', fault.endpoint
  );
  try {
    await subscriber.waitReady();
    let offset = await evidenceCount(reconnectSubscriberUrl);
    fault.unblock();
    await waitForConnection(reconnectSubscriberUrl, offset);
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 1, 'before-disconnect');
    await Promise.all([
      ...fastSubscribers.map((url) => waitForEvent(url, runId, 1, 'before-disconnect')),
      waitForEvent(reconnectSubscriberUrl, runId, 1, 'before-disconnect')
    ]);

    offset = await evidenceCount(reconnectSubscriberUrl);
    fault.block();
    await waitForDisconnection(reconnectSubscriberUrl, offset);
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 2, 'while-disconnected');
    await Promise.all(fastSubscribers.map((url) => waitForEvent(url, runId, 2, 'while-disconnected')));

    offset = await evidenceCount(reconnectSubscriberUrl);
    fault.unblock();
    await waitForConnection(reconnectSubscriberUrl, offset);
    await publishEvent(publisher, PubSubNames.mainTopic, runId, 3, 'after-reconnect');
    await Promise.all([
      ...fastSubscribers.map((url) => waitForEvent(url, runId, 3, 'after-reconnect')),
      waitForEvent(reconnectSubscriberUrl, runId, 3, 'after-reconnect')
    ]);
    const evidence = await getJson<readonly string[]>(reconnectSubscriberUrl, '/evidence');
    ensure(
      !subscriber.hasExited
        && evidence.every((line) => !line.includes(`run=${runId}`) || !line.includes('seq=2|')),
      'PS-A4 restarted the application or replayed the disconnected event.'
    );
    console.log('scenario PS-A4 passed');
  } finally {
    await subscriber.kill();
    await fault.close();
  }
}
