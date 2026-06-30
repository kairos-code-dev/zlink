import { ZLinkServiceRole, ZLinkTopologyState } from '@zlink-systems/framework';
import type { ProfileReply } from '../../Shared/messages';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRmA1(providerAUrl: string, providerBUrl: string, registryUrl: string): Promise<void> {
  const reply = await postJson<ProfileReply>(providerAUrl, '/profile/request', { value: 'rm-a1' });
  ensure(reply.value === 'profile:rm-a1', 'RM-A1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'RM-A1 provider rid was not api-a/api-b.');

  const topology = await getJson<Array<{ channelName: string; serviceRole: number; state: number }>>(registryUrl, '/registry/topology');
  const readyProfileProviders = topology.filter((entry) =>
    entry.channelName === 'profile'
    && entry.serviceRole === ZLinkServiceRole.Router
    && entry.state === ZLinkTopologyState.Ready
  ).length;
  ensure(readyProfileProviders >= 2, 'RM-A1 expected two ready profile providers in topology.');

  const providerEvidence = [
    ...await getJson<string[]>(providerAUrl, '/evidence'),
    ...await getJson<string[]>(providerBUrl, '/evidence')
  ];
  ensure(providerEvidence.some((line) => line.includes('value=rm-a1')), 'RM-A1 provider evidence missing.');
  console.log('scenario RM-A1 passed');
}
