import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForEvidence, waitForMemberPeer } from '../Support/discovery-scenario-support';

export async function runDrB2(options: ClientOptions): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-B2 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-b2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-b2', marker });
  ensure(reply.value === 'profile:dr-b2', 'DR-B2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-B2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-B2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-B2 provider evidence was not recorded.'
  );
  console.log('scenario DR-B2 passed');
}
