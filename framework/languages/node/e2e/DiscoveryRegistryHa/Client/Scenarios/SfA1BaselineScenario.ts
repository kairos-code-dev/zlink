// SF-A1: store 정상 상태 baseline 시나리오를 검증한다.
import {
  ZLinkLocationRole,
  ZLinkLocationTopologyState
} from '@zlink-systems/framework';
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSfA1(options: ClientOptions): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'SF-A1 requires provider-b-url.');
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'sf-a1' });
  ensure(reply.value === 'profile:sf-a1', 'SF-A1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'SF-A1 provider rid mismatch.');

  const topology = await getJson<Array<{ channelName: string; serviceRole: number; state: number }>>(
    options.topologyUrl,
    '/location/topology'
  );
  const readyProviders = topology.filter((entry) =>
    entry.channelName === 'profile'
    && entry.serviceRole === ZLinkLocationRole.Router
    && entry.state === ZLinkLocationTopologyState.Ready
  ).length;
  ensure(readyProviders >= 2, 'SF-A1 expected two ready providers in location topology.');

  const providerEvidence = [
    ...await getJson<string[]>(options.providerAUrl, '/evidence'),
    ...await getJson<string[]>(options.providerBUrl, '/evidence')
  ];
  ensure(providerEvidence.some((entry) => entry.includes('value=sf-a1')), 'SF-A1 provider evidence missing.');
  console.log('scenario SF-A1 passed');
}
