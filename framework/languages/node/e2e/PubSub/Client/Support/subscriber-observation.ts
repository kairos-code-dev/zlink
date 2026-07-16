import { getJson, postJsonWithin } from '../../../http-client';
import { PubSubNames } from '../../Shared/messages';
import { eventually } from './scenario-assert';

interface PublisherObservation {
  readonly rid: string;
  readonly endpoint: string;
}

export async function evidenceCount(subscriberUrl: string): Promise<number> {
  return (await getJson<readonly string[]>(subscriberUrl, '/evidence')).length;
}

export async function waitForConnection(subscriberUrl: string, afterIndex = 0): Promise<readonly string[]> {
  return await waitForSocketKind(subscriberUrl, ['connectionReady'], afterIndex);
}

export async function waitForDisconnection(subscriberUrl: string, afterIndex = 0): Promise<readonly string[]> {
  return await waitForSocketKind(subscriberUrl, ['disconnected', 'closed'], afterIndex);
}

export async function waitForEvent(
  subscriberUrl: string,
  runId: string,
  sequence: number,
  value: string
): Promise<readonly string[]> {
  return await postJsonWithin<readonly string[]>(subscriberUrl, '/evidence/wait', {
    containsAllLineGroups: [[
      'event|',
      `run=${runId}`,
      `topic=${PubSubNames.mainTopic}`,
      `seq=${sequence}|`,
      `value=${value}`
    ]],
    timeoutMilliseconds: 10_000
  }, 11_000);
}

export async function waitForPublisherRow(
  subscriberUrl: string,
  present: boolean,
  endpoint?: string
): Promise<void> {
  await eventually(async () => {
    const rows = await getJson<readonly PublisherObservation[]>(subscriberUrl, '/locations/peers');
    const observed = rows.some((row) =>
      row.rid === 'pub-a' && (endpoint === undefined || row.endpoint === endpoint));
    return observed === present;
  }, `Publisher row 'pub-a' did not become present=${present}.`, 30_000);
}

async function waitForSocketKind(
  subscriberUrl: string,
  kinds: readonly string[],
  afterIndex: number
): Promise<readonly string[]> {
  let observed: readonly string[] = [];
  await eventually(async () => {
    observed = await getJson<readonly string[]>(subscriberUrl, '/evidence');
    return observed.slice(afterIndex).some((line) =>
      line.includes('monitor-socket|')
      && line.includes(`source=${PubSubNames.channel}.subscriber`)
      && kinds.some((kind) => line.includes(`kind=${kind}`))
      && (!kinds.includes('connectionReady') || !line.includes('routing=<null>')));
  }, `Subscriber socket event did not reach one of: ${kinds.join(', ')}.`);
  return observed;
}
