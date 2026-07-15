// RM-A1: location store 자동 연결 + rid 자동 resolve 시나리오를 검증한다.
import { ZLinkLocationRole } from '@zlink-systems/framework';
import type { ProfileRes } from '../../Shared/messages';
import { getJson, postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRmA1(locationConsumerUrl: string, providerAUrl: string, providerBUrl: string): Promise<void> {
  const observedProviders = new Set<string>();
  for (let attempt = 0; attempt < 40 && observedProviders.size < 2; attempt += 1) {
    const reply = await postJson<ProfileRes>(locationConsumerUrl, '/profile/request', { value: `rm-a1-${attempt}` });
    ensure(reply.value === `profile:rm-a1-${attempt}`, 'RM-A1 reply value mismatch.');
    observedProviders.add(reply.providerRid);
  }
  ensure(observedProviders.has('api-a') && observedProviders.has('api-b'), 'RM-A1 did not route through both providers.');

  const topology = await getJson<Array<{ channelName: string; serviceRole: number; routingId?: string; endpoint: string }>>(
    locationConsumerUrl,
    '/location/topology'
  );
  const profileProviders = topology.filter((entry) =>
    entry.channelName === 'profile'
    && entry.serviceRole === ZLinkLocationRole.Router
    && (entry.routingId === 'api-a' || entry.routingId === 'api-b')
    && entry.endpoint.length > 0
  );
  ensure(
    profileProviders.length >= 2,
    `RM-A1 expected live peer rows for both profile providers: ${JSON.stringify(topology)}`
  );

  const providerEvidence = [
    ...await getJson<string[]>(providerAUrl, '/evidence'),
    ...await getJson<string[]>(providerBUrl, '/evidence')
  ];
  ensure(providerEvidence.some((line) => line.includes('rid=api-a') && line.includes('value=rm-a1-')), 'RM-A1 api-a connection evidence missing.');
  ensure(providerEvidence.some((line) => line.includes('rid=api-b') && line.includes('value=rm-a1-')), 'RM-A1 api-b connection evidence missing.');
  console.log('scenario RM-A1 passed');
}
