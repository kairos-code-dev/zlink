// MON-A1: socket 이벤트 관찰 시나리오를 검증한다.
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

interface SocketEvidence {
  readonly source: string;
  readonly kind: string;
  readonly remoteAddr: string;
  readonly routingId: string;
}

export async function runMonA1(options: ClientOptions): Promise<void> {
  const request: ProfileReq = { value: 'monitor', marker: 'mon-a1-request' };
  const reply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/disconnect', request);
  ensure(reply.value === 'profile:monitor', 'MON-A1 trigger request failed.');

  const evidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-socket|', `source=${RuntimeMonitoringNames.channelServerSource}`],
    containsAnyGroups: [['kind=connected', 'kind=connectionReady'], ['kind=disconnected', 'kind=closed']],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);

  const socketEvents = evidence
    .map(parseSocketEvidence)
    .filter((event): event is SocketEvidence => event !== undefined);
  const disconnected = socketEvents.find((event) =>
    event.source === RuntimeMonitoringNames.channelServerSource
    && (event.kind === 'disconnected' || event.kind === 'closed')
    && event.remoteAddr.startsWith('tcp://')
    && event.routingId !== '<null>');
  ensure(disconnected !== undefined, 'MON-A1 identified socket disconnect evidence missing.');

  const connected = socketEvents.find((event) =>
    event.source === RuntimeMonitoringNames.channelServerSource
    && event.kind === 'connectionReady'
    && event.remoteAddr.startsWith('tcp://')
    && event.routingId !== '<null>'
    && event.remoteAddr === disconnected.remoteAddr
    && event.routingId === disconnected.routingId);
  ensure(connected !== undefined, 'MON-A1 matching socket connection evidence missing.');

  console.log('scenario MON-A1 passed');
}

function parseSocketEvidence(line: string): SocketEvidence | undefined {
  if (!line.startsWith('monitor-socket|')) {
    return undefined;
  }
  const fields = new Map<string, string>();
  for (const part of line.split('|').slice(1)) {
    const separator = part.indexOf('=');
    if (separator > 0) {
      fields.set(part.slice(0, separator), part.slice(separator + 1));
    }
  }
  const source = fields.get('source');
  const kind = fields.get('kind');
  const remoteAddr = fields.get('remote');
  const routingId = fields.get('routing');
  if (source === undefined || kind === undefined || remoteAddr === undefined || routingId === undefined) {
    return undefined;
  }
  return { source, kind, remoteAddr, routingId };
}
