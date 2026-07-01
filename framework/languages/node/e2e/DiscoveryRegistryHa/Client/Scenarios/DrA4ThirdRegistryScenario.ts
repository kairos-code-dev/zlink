import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForEitherProviderEvidence } from '../Support/discovery-scenario-support';

export async function runDrA4(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A4 requires registry-2-url.');
  ensure(options.duplicateProviderUrl !== undefined, 'DR-A4 requires duplicate-provider-url.');

  const marker = `dr-a4-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: 'dr-a4', marker });
  ensure(reply.value === 'profile:dr-a4', 'DR-A4 reply value mismatch.');
  ensure(reply.providerRid === 'api-a', 'DR-A4 same-rid providers should report api-a.');
  ensure(reply.marker === marker, 'DR-A4 marker mismatch.');

  const evidence = await waitForEitherProviderEvidence(options.providerAUrl, options.duplicateProviderUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes('rid=api-a')),
    'DR-A4 provider evidence was not recorded.'
  );
  console.log('scenario DR-A4 passed');
}
