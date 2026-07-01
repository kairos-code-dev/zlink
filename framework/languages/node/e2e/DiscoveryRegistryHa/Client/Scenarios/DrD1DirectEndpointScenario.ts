import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { waitForEvidence } from '../Support/discovery-scenario-support';

export async function runDrD1(options: ClientOptions): Promise<void> {
  ensure(options.embeddedUrl !== undefined, 'DR-D1 requires embedded-url.');
  ensure(options.embeddedConsumerUrl !== undefined, 'DR-D1 requires embedded-consumer-url.');

  const marker = `dr-d1-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(options.embeddedConsumerUrl, '/profile/request', { value: 'dr-d1', marker });
  ensure(reply.value === 'profile:dr-d1', 'DR-D1 reply value mismatch.');
  ensure(reply.providerRid === 'embedded-api', 'DR-D1 should route to embedded-api.');
  ensure(reply.marker === marker, 'DR-D1 marker mismatch.');

  const evidence = await waitForEvidence(options.embeddedUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes('rid=embedded-api')),
    'DR-D1 embedded provider evidence was not recorded.'
  );
  console.log('scenario DR-D1 passed');
}
