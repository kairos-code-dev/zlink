import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForMemberPeer } from '../Support/discovery-scenario-support';

export async function runDrA2(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A2 requires registry-2-url.');

  const members = await waitForMemberPeer(options.registry2Url, 'api-a');
  ensure(members.some((entry) => String(entry.routingId) === 'api-a'), 'DR-A2 reg-2 member peer did not include api-a.');

  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-a2' });
  ensure(reply.value === 'profile:dr-a2', 'DR-A2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a', 'DR-A2 expected reg-1 provider api-a through reg-2 discovery.');

  const providerEvidence = await getJson<string[]>(options.providerAUrl, '/evidence');
  ensure(providerEvidence.some((entry) => entry.includes('value=dr-a2')), 'DR-A2 provider evidence missing.');
  console.log('scenario DR-A2 passed');
}
