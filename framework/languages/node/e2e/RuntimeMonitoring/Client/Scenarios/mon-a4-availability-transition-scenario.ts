// MON-A4: 가용성 전이 관측 (failover / drain) 시나리오를 검증한다.
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA4(options: ClientOptions): Promise<void> {
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

  console.log('scenario MON-A4 passed');
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
