import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForConnectedPeerRegistryCount, waitForMemberPeers } from '../Support/discovery-scenario-support';

export async function runDrA3(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A3 requires registry-2-url.');
  ensure(options.registry3Url !== undefined, 'DR-A3 requires registry-3-url.');
  ensure(options.consumer2Url !== undefined, 'DR-A3 requires consumer-2-url.');
  ensure(options.consumer3Url !== undefined, 'DR-A3 requires consumer-3-url.');
  ensure(options.providerBUrl !== undefined, 'DR-A3 requires provider-b-url.');

  for (const registryUrl of [options.registryUrl, options.registry2Url, options.registry3Url]) {
    await waitForConnectedPeerRegistryCount(registryUrl, 2);
    await waitForMemberPeers(registryUrl, ['api-a', 'api-b']);
  }

  const checks = [
    { url: options.consumerUrl, value: 'dr-a3-reg-1' },
    { url: options.consumer2Url, value: 'dr-a3-reg-2' },
    { url: options.consumer3Url, value: 'dr-a3-reg-3' }
  ];
  for (const check of checks) {
    const reply = await postJson<ProfileRes>(check.url, '/profile/request', { value: check.value });
    ensure(reply.value === `profile:${check.value}`, `DR-A3 reply value mismatch for ${check.value}.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `DR-A3 provider rid mismatch for ${check.value}.`);
  }

  const providerEvidence = [
    ...await getJson<string[]>(options.providerAUrl, '/evidence'),
    ...await getJson<string[]>(options.providerBUrl, '/evidence')
  ];
  for (const check of checks) {
    ensure(
      providerEvidence.some((entry) => entry.includes(`value=${check.value}`)),
      `DR-A3 provider evidence missing for ${check.value}.`
    );
  }
  console.log('scenario DR-A3 passed');
}
