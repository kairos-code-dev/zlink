import {
  ZLinkServiceRole,
  ZLinkTopologyState
} from '@zlink-systems/framework';
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runDrA1(options: ClientOptions): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-A1 requires provider-b-url.');
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-a1' });
  ensure(reply.value === 'profile:dr-a1', 'DR-A1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-A1 provider rid mismatch.');

  const topology = await getJson<Array<{ channelName: string; serviceRole: number; state: number }>>(
    options.registryUrl,
    '/registry/topology'
  );
  const readyProviders = topology.filter((entry) =>
    entry.channelName === 'profile'
    && entry.serviceRole === ZLinkServiceRole.Router
    && entry.state === ZLinkTopologyState.Ready
  ).length;
  ensure(readyProviders >= 2, 'DR-A1 expected two ready providers in registry topology.');

  const providerEvidence = [
    ...await getJson<string[]>(options.providerAUrl, '/evidence'),
    ...await getJson<string[]>(options.providerBUrl, '/evidence')
  ];
  ensure(providerEvidence.some((entry) => entry.includes('value=dr-a1')), 'DR-A1 provider evidence missing.');
  console.log('scenario DR-A1 passed');
}
