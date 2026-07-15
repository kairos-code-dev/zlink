// MON-A4: 가용성 전이 관측 (failover / drain) 시나리오를 검증한다.
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import {
  type ManagedProcess,
  startReplacementService,
  waitForPortState
} from '../Support/managed-service';
import { ensure } from '../Support/scenario-assert';

interface TopologyObservation {
  readonly eventCount: number;
  readonly endpoints: readonly string[];
}

export async function runMonA4(options: ClientOptions): Promise<ManagedProcess> {
  const beforeFailover = await waitForTopologyEndpoint(
    options,
    undefined,
    options.serviceBChannelEndpoint
  );
  await postJson<object>(options.serviceBUrl, '/shutdown', {});
  await waitForPortState(options.serviceBUrl, false, 'MON-A4 expected the original svc-b endpoint to stop.');

  const replacement = startReplacementService(options, 'svc-b-mon-a4-replacement');
  try {
    await waitForPortState(
      options.replacementServiceUrl,
      true,
      'MON-A4 expected the replacement svc-b endpoint to start.'
    );
    const afterFailover = await waitForTopologyEndpoint(
      options,
      beforeFailover,
      options.replacementServiceChannelEndpoint
    );
    ensure(
      beforeFailover.endpoint !== afterFailover.endpoint,
      'MON-A4 topology payload did not replace the svc-b endpoint.'
    );
    ensure(
      !afterFailover.endpoints.includes(options.serviceBChannelEndpoint),
      'MON-A4 topology payload retained the old svc-b channel endpoint.'
    );
    await waitForFailoverSocketEvidence(options);

    await verifyDrainTransition(options);
    console.log('scenario MON-A4 passed');
    return replacement;
  } catch (error) {
    await replacement.stop();
    throw error;
  }
}

async function verifyDrainTransition(options: ClientOptions): Promise<void> {
  const before = await postJson<ProfileRes>(
    options.triggerUrl,
    '/profile/request',
    { value: 'drain', marker: 'mon-a4-before-drain' } satisfies ProfileReq
  );
  ensure(before.providerRid === 'svc-a', 'MON-A4 direct trigger did not hit drained service.');

  await postJson(options.serviceUrl, '/admin/drain');
  await waitForWeight(options.serviceUrl, 0);
  const triggerEvidence = await waitForTriggerDrainEvidence(options.triggerUrl);

  await postJson(options.serviceUrl, '/admin/restore');
  await waitForWeight(options.serviceUrl, 100);

  ensure(
    triggerEvidence.some((line) =>
      line.includes('monitor-socket|')
      && line.includes(`source=${RuntimeMonitoringNames.channelClientSource}`)
      && line.includes('kind=peerAdmissionChanged')),
    'MON-A4 trigger socket drain transition evidence missing.'
  );

  const serviceEvidence = await waitForServiceDrainEvidence(options.serviceUrl);
  ensure(
    serviceEvidence.some((line) =>
      line.includes('admin|') && line.includes('action=drain') && line.includes('weight=0')),
    'MON-A4 service drain evidence missing.'
  );

  const locationEvidence = await waitForLocationTopologyEvidence(options.serviceUrl);
  ensure(
    locationEvidence.filter((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged')).length >= 2,
    'MON-A4 location topology transition evidence missing.'
  );

}

async function waitForTopologyEndpoint(
  options: ClientOptions,
  after: TopologyObservation | undefined,
  expectedEndpoint: string
): Promise<TopologyObservation & { readonly endpoint: string }> {
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const observations = topologyObservations(await getJson<string[]>(options.serviceUrl, '/evidence'));
    const current = observations.at(-1);
    const endpoint = current?.endpoints.find((value) => value === expectedEndpoint);
    if (current !== undefined && endpoint !== undefined && (after === undefined || current.eventCount > after.eventCount)) {
      return { ...current, endpoint };
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`MON-A4 timed out waiting for topology endpoint '${expectedEndpoint}'.`);
}

function topologyObservations(lines: readonly string[]): TopologyObservation[] {
  const topologyLines = lines.filter((line) =>
    line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged'));
  return topologyLines.map((line, index) => {
    const nodes = line.match(/(?:^|\|)topologyNodes=([^|]*)/)?.[1] ?? '';
    const endpoints = nodes.split(',')
      .filter((value) => value.startsWith('svc-b@'))
      .map((value) => value.slice('svc-b@'.length).replace(/:\d+$/, ''));
    return { eventCount: index + 1, endpoints };
  });
}

async function waitForFailoverSocketEvidence(options: ClientOptions): Promise<void> {
  const evidence = await postJson<string[]>(options.triggerUrl, '/evidence/wait', {
    containsAll: [
      `kind=disconnected|remote=${options.serviceBChannelEndpoint}`,
      `kind=connectionReady|remote=${options.replacementServiceChannelEndpoint}`
    ],
    containsAnyGroups: [
      [`kind=connected|remote=${options.replacementServiceChannelEndpoint}`]
    ],
    timeoutMilliseconds: 20_000
  } satisfies EvidenceWaitReq);
  ensure(
    evidence.some((line) => line.includes(`kind=disconnected|remote=${options.serviceBChannelEndpoint}`)),
    'MON-A4 old endpoint disconnect evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes(`kind=connectionReady|remote=${options.replacementServiceChannelEndpoint}`)),
    'MON-A4 replacement endpoint connection-ready evidence missing.'
  );
}

async function waitForWeight(serviceUrl: string, expected: number): Promise<void> {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const result = await getJson<{ readonly weight: number }>(serviceUrl, '/admin/weight');
    if (result.weight === expected) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Service weight did not become ${expected}.`);
}

async function waitForTriggerDrainEvidence(triggerUrl: string): Promise<string[]> {
  return await postJson<string[]>(triggerUrl, '/evidence/wait', {
    containsAll: ['monitor-socket|', `source=${RuntimeMonitoringNames.channelClientSource}`],
    containsAnyGroups: [['kind=peerAdmissionChanged']],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
}

async function waitForServiceDrainEvidence(serviceUrl: string): Promise<string[]> {
  return await postJson<string[]>(serviceUrl, '/evidence/wait', {
    containsAll: ['admin|', 'action=drain'],
    containsAnyGroups: [['weight=0']],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
}

async function waitForLocationTopologyEvidence(serviceUrl: string): Promise<string[]> {
  return await postJson<string[]>(serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-location|source=monitor.location-runtime'],
    containsAnyGroups: [['kind=TopologyChanged']],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
}
