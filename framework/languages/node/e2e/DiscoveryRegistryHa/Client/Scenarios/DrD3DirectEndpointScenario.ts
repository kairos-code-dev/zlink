import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForEvidence, waitForMemberPeer } from '../Support/discovery-scenario-support';

export async function runDrD3(options: ClientOptions): Promise<void> {
  ensure(options.embeddedUrl !== undefined, 'DR-D3 requires embedded-url.');
  ensure(options.embeddedConsumerUrl !== undefined, 'DR-D3 requires embedded-consumer-url.');
  ensure(options.providerBUrl !== undefined, 'DR-D3 requires provider-b-url.');

  await waitForMemberPeer(options.embeddedUrl, 'api-a');

  const marker = `dr-d3-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.embeddedConsumerUrl, '/profile/request', { value: 'dr-d3', marker });
  ensure(reply.value === 'profile:dr-d3', 'DR-D3 reply value mismatch.');
  ensure(
    reply.providerRid === 'api-a' || reply.providerRid === 'api-b' || reply.providerRid === 'embedded-api-mixed',
    'DR-D3 provider rid mismatch.'
  );
  ensure(reply.marker === marker, 'DR-D3 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a'
    ? options.providerAUrl
    : reply.providerRid === 'api-b'
      ? options.providerBUrl
      : options.embeddedUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-D3 provider evidence was not recorded.'
  );
  console.log('scenario DR-D3 passed');
}
