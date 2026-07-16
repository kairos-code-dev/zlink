// MON-A4: replacement, SIGKILL failover, transport weight 제외를 분리해 관측한다.
import type { ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson, postJsonWithin } from '../../../http-client';
import {
  type ManagedProcess,
  postBestEffort,
  startReplacementService,
  waitForPortState
} from '../Support/managed-service';
import { ensure } from '../Support/scenario-assert';

interface TopologyObservation {
  readonly eventCount: number;
  readonly endpoints: readonly string[];
}

interface PeerObservation {
  readonly rid: string;
  readonly endpoint: string;
}

export async function runMonA4(options: ClientOptions): Promise<ManagedProcess> {
  const beforeFailover = await waitForTopologyEndpoint(options.serviceUrl, undefined, options.serviceBChannelEndpoint);
  const drain = await postJsonWithin<{ readonly kind: string; readonly reason?: string }>(
    options.serviceBUrl, '/admin/drain', {}, 35_000
  );
  ensure(drain.kind === 'drained' && drain.reason === undefined, 'MON-A4 replacement did not reach terminal Drained.');
  await waitForPeer(options.serviceUrl, 'svc-b', false);
  await postJson<object>(options.serviceBUrl, '/shutdown', {});
  await waitForPortState(options.serviceBUrl, false, 'MON-A4 expected the original svc-b endpoint to stop.');

  const replacement = startReplacementService(options, 'svc-b-mon-a4-replacement');
  try {
    await waitForPortState(options.replacementServiceUrl, true, 'MON-A4 expected replacement svc-b to start.');
    const afterFailover = await waitForTopologyEndpoint(
      options.serviceUrl, beforeFailover, options.replacementServiceChannelEndpoint
    );
    ensure(beforeFailover.endpoint !== afterFailover.endpoint, 'MON-A4 replacement kept the old endpoint.');
    ensure(!afterFailover.endpoints.includes(options.serviceBChannelEndpoint), 'MON-A4 retained the old svc-b row.');
    await waitForReplacementSocketEvidence(options);

    await verifyCrashFailover(options);
    await verifyWeightExclusion(options);
    console.log('scenario MON-A4 passed');
    return replacement;
  } catch (error) {
    await replacement.stop();
    throw error;
  }
}

async function verifyCrashFailover(options: ClientOptions): Promise<void> {
  const [fromA, fromB] = await Promise.all([
    requestProfile(options.triggerUrl, '/profile/request/disconnect', 'before-crash-a'),
    requestProfile(options.triggerUrl, '/profile/request/replacement', 'before-crash-b')
  ]);
  ensure(fromA.providerRid === 'svc-a' && fromB.providerRid === 'svc-b', 'MON-A4 did not establish two live providers.');

  const triggerOffset = await evidenceCount(options.triggerUrl);
  const observerOffset = await evidenceCount(options.replacementServiceUrl);
  await postBestEffort(options.serviceUrl, '/crash'); // 실제 SIGKILL은 provider가 자기 process에 보낸다.
  await waitForPortState(options.serviceUrl, false, 'MON-A4 expected svc-a SIGKILL to close its port.');
  await waitForPeer(options.replacementServiceUrl, 'svc-a', false); // owner lease 만료 뒤 topology 제외
  await waitForEvidence(options.triggerUrl, triggerOffset, (line) =>
    line.includes(`kind=disconnected|remote=${options.serviceChannelEndpoint}`));
  await waitForEvidence(options.replacementServiceUrl, observerOffset, (line) =>
    line.includes('monitor-location|') && line.includes('kind=TopologyChanged'));

  const followUp = await requestProfile(
    options.triggerUrl, '/profile/request/replacement', 'after-lease-follow-up'
  );
  ensure(followUp.providerRid === 'svc-b', 'MON-A4 lease expiry follow-up did not reach provider B.');
}

async function verifyWeightExclusion(options: ClientOptions): Promise<void> {
  const triggerOffset = await evidenceCount(options.triggerUrl);
  await postJson(options.replacementServiceUrl, '/admin/exclude');
  await waitForWeight(options.replacementServiceUrl, 0);
  await waitForEvidence(options.triggerUrl, triggerOffset, (line) =>
    line.includes('monitor-socket|')
      && line.includes(`source=${RuntimeMonitoringNames.channelClientSource}`)
      && line.includes('kind=peerAdmissionChanged'));
  const serviceEvidence = await getJson<readonly string[]>(options.replacementServiceUrl, '/evidence');
  ensure(
    serviceEvidence.some((line) => line.includes('admin|') && line.includes('action=exclude') && line.includes('weight=0')),
    'MON-A4 transport exclusion evidence is missing.'
  );
  await postJson(options.replacementServiceUrl, '/admin/include');
  await waitForWeight(options.replacementServiceUrl, 100);
}

async function requestProfile(baseUrl: string, path: string, marker: string): Promise<ProfileRes> {
  return await postJson<ProfileRes>(baseUrl, path, {
    value: marker,
    marker
  } satisfies ProfileReq);
}

async function waitForPeer(observerUrl: string, rid: string, present: boolean): Promise<void> {
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const rows = await getJson<readonly PeerObservation[]>(observerUrl, '/locations/peers');
    if (rows.some((row) => row.rid === rid) === present) return;
    await delay();
  }
  throw new Error(`MON-A4 peer '${rid}' did not become present=${present}.`);
}

async function waitForTopologyEndpoint(
  observerUrl: string,
  after: TopologyObservation | undefined,
  expectedEndpoint: string
): Promise<TopologyObservation & { readonly endpoint: string }> {
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const observations = topologyObservations(await getJson<string[]>(observerUrl, '/evidence'));
    const current = observations.at(-1);
    const endpoint = current?.endpoints.find((value) => value === expectedEndpoint);
    if (current !== undefined && endpoint !== undefined && (after === undefined || current.eventCount > after.eventCount)) {
      return { ...current, endpoint };
    }
    await delay();
  }
  throw new Error(`MON-A4 timed out waiting for topology endpoint '${expectedEndpoint}'.`);
}

function topologyObservations(lines: readonly string[]): TopologyObservation[] {
  return lines
    .filter((line) => line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged'))
    .map((line, index) => {
      const nodes = line.match(/(?:^|\|)topologyNodes=([^|]*)/)?.[1] ?? '';
      return {
        eventCount: index + 1,
        endpoints: nodes.split(',').map((value) => value.split('@')[1]?.replace(/:\d+$/, '') ?? '').filter(Boolean)
      };
    });
}

async function waitForReplacementSocketEvidence(options: ClientOptions): Promise<void> {
  await Promise.all([
    waitForEvidence(options.triggerUrl, 0, (line) =>
      line.includes(`kind=disconnected|remote=${options.serviceBChannelEndpoint}`)),
    waitForEvidence(options.triggerUrl, 0, (line) =>
      line.includes(`kind=connectionReady|remote=${options.replacementServiceChannelEndpoint}`))
  ]);
}

async function evidenceCount(url: string): Promise<number> {
  return (await getJson<readonly string[]>(url, '/evidence')).length;
}

async function waitForEvidence(
  url: string,
  afterIndex: number,
  predicate: (line: string) => boolean
): Promise<void> {
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const evidence = await getJson<readonly string[]>(url, '/evidence');
    if (evidence.slice(afterIndex).some(predicate)) return;
    await delay();
  }
  throw new Error('MON-A4 timed out waiting for monitoring evidence.');
}

async function waitForWeight(serviceUrl: string, expected: number): Promise<void> {
  const deadline = Date.now() + 10_000;
  while (Date.now() < deadline) {
    if ((await getJson<{ readonly weight: number }>(serviceUrl, '/admin/weight')).weight === expected) return;
    await delay();
  }
  throw new Error(`Service weight did not become ${expected}.`);
}

function delay(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 100));
}
