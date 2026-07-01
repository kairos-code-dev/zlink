import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { assertDeadRegistryFails, waitForEvidence, waitForMemberPeer } from '../Support/discovery-scenario-support';

export async function runDrC1(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-C1 requires registry-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-C1 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-c1-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-c1', marker });
  ensure(reply.value === 'profile:dr-c1', 'DR-C1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-C1 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-C1 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-C1 provider evidence was not recorded.'
  );

  await assertDeadRegistryFails(options.registry2Url);
  console.log('scenario DR-C1 passed');
}
