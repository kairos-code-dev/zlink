import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForConnectedPeerRegistryCount, waitForEvidence, waitForMemberPeers } from '../Support/discovery-scenario-support';

export async function runDrB3(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-B3 requires registry-2-url.');
  ensure(options.consumer2Url !== undefined, 'DR-B3 requires consumer-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-B3 requires provider-b-url.');

  const cases = [
    { name: 'reg-2', registryUrl: options.registry2Url, consumerUrl: options.consumer2Url },
    { name: 'survivor', registryUrl: options.registryUrl, consumerUrl: options.consumerUrl }
  ];
  for (const registryCase of cases) {
    await waitForConnectedPeerRegistryCount(registryCase.registryUrl, 1);
    await waitForMemberPeers(registryCase.registryUrl, ['api-a', 'api-b']);

    const marker = `dr-b3-${registryCase.name}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const reply = await postJson<ProfileRes>(registryCase.consumerUrl, '/profile/request', { value: 'dr-b3', marker });
    ensure(reply.value === 'profile:dr-b3', `DR-B3 ${registryCase.name} reply value mismatch.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `DR-B3 ${registryCase.name} provider rid mismatch.`);
    ensure(reply.marker === marker, `DR-B3 ${registryCase.name} marker mismatch.`);

    const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
    const evidence = await waitForEvidence(evidenceUrl, marker);
    ensure(
      evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
      `DR-B3 ${registryCase.name} provider evidence was not recorded.`
    );
  }
  console.log('scenario DR-B3 passed');
}
