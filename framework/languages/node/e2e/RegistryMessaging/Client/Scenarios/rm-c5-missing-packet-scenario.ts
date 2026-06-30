import type { ProfileReply, RequestFailureResult } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRmC5(discoveryConsumerUrl: string, providerAUrl: string, providerBUrl: string): Promise<void> {
  const missingRequest = await postJson<RequestFailureResult>(discoveryConsumerUrl, '/profile/missing-request', { value: 'missing-request' });
  ensure(missingRequest.failed, 'RM-C5 missing request should fail.');
  await postJson(discoveryConsumerUrl, '/profile/missing-command', { commandId: 'missing-send' });
  const evidence = [
    ...await waitForDispatchErrorEvidence(providerAUrl, providerBUrl, 'MissingProfileRequest'),
    ...await waitForDispatchErrorEvidence(providerAUrl, providerBUrl, 'MissingProfileCommand')
  ];
  ensure(evidence.some((line) => line.includes('dispatch-error') && line.includes('MissingProfileRequest')), 'RM-C5 missing request evidence missing.');
  ensure(evidence.some((line) => line.includes('dispatch-error') && line.includes('MissingProfileCommand')), 'RM-C5 missing send evidence missing.');
  const reply = await postJson<ProfileReply>(discoveryConsumerUrl, '/profile/request', { value: 'rm-c5-after' });
  ensure(reply.value === 'profile:rm-c5-after', 'RM-C5 normal request after negative path failed.');
  console.log('scenario RM-C5 passed');
}

async function waitForDispatchErrorEvidence(providerAUrl: string, providerBUrl: string, packetName: string): Promise<string[]> {
  const result = await Promise.all([
    postJson<string[]>(providerAUrl, '/evidence/wait', { contains: packetName }),
    postJson<string[]>(providerBUrl, '/evidence/wait', { contains: packetName })
  ]);
  return result.flat();
}
