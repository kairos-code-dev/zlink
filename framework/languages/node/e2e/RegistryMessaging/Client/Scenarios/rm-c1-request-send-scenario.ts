import type { ProfileReply } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure, uniqueMarker } from '../Support/scenario-assert';

export async function runRmC1(providerAUrl: string, providerBUrl: string): Promise<void> {
  const reply = await postJson<ProfileReply>(providerAUrl, '/profile/request', { value: 'rm-c1-request' });
  ensure(reply.value === 'profile:rm-c1-request', 'RM-C1 request reply mismatch.');

  const commandId = uniqueMarker('cmd');
  await postJson(providerAUrl, '/profile/command', { commandId });
  const evidence = await Promise.all([
    postJson<string[]>(providerAUrl, '/evidence/wait', { contains: commandId }),
    postJson<string[]>(providerBUrl, '/evidence/wait', { contains: commandId })
  ]);
  const lines = evidence.flat();
  ensure(lines.some((line) => line.includes('profile-command|')), 'RM-C1 send evidence missing.');
  console.log('scenario RM-C1 passed');
}
