import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForConnectedPeerRegistryCount, waitForEvidence, waitForMemberPeers } from '../Support/discovery-scenario-support';

export async function runDrC2(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-C2 requires registry-2-url.');
  ensure(options.consumer2Url !== undefined, 'DR-C2 requires consumer-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-C2 requires provider-b-url.');

  await waitForConnectedPeerRegistryCount(options.registry2Url, 1);
  await waitForMemberPeers(options.registry2Url, ['api-a', 'api-b']);

  const marker = `dr-c2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.consumer2Url, '/profile/request', { value: 'dr-c2', marker });
  ensure(reply.value === 'profile:dr-c2', 'DR-C2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-C2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-C2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-C2 provider evidence was not recorded.'
  );
  console.log('scenario DR-C2 passed');
}
