import type { ProfileReply } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRmA2(providerAUrl: string): Promise<void> {
  const reply = await postJson<ProfileReply>(providerAUrl, '/profile/manual', { value: 'rm-a2' });
  ensure(reply.value === 'profile:rm-a2', 'RM-A2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a', 'RM-A2 manual endpoint should reach api-a.');
  const evidence = await postJson<string[]>(providerAUrl, '/evidence/wait', { contains: 'value=rm-a2' });
  ensure(evidence.some((line) => line.includes('value=rm-a2')), 'RM-A2 api-a evidence missing.');
  console.log('scenario RM-A2 passed');
}
