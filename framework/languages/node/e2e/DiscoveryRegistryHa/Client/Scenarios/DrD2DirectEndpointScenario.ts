import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForEvidence, waitForMemberPeer } from '../Support/discovery-scenario-support';

export async function runDrD2(options: ClientOptions): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-D2 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-d2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-d2', marker });
  ensure(reply.value === 'profile:dr-d2', 'DR-D2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-D2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-D2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-D2 provider evidence was not recorded.'
  );
  console.log('scenario DR-D2 passed');
}
