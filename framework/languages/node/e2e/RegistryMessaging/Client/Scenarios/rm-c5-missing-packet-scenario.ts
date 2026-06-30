import type { ProfileRes, RequestFailureRes } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runRmC5(discoveryConsumerUrl: string, providerAUrl: string, providerBUrl: string): Promise<void> {
  const missingRequest = await postJson<RequestFailureRes>(discoveryConsumerUrl, '/profile/missing-request', { value: 'missing-request' });
  ensure(missingRequest.failed, 'RM-C5 missing request should fail.');
  await postJson(discoveryConsumerUrl, '/profile/missing-command', { commandId: 'missing-send' });
  const evidence = [
    ...await waitForDispatchErrorEvidence(providerAUrl, providerBUrl, 'MissingProfileReq'),
    ...await waitForDispatchErrorEvidence(providerAUrl, providerBUrl, 'MissingProfileMsg')
  ];
  ensure(evidence.some((line) => line.includes('dispatch-error') && line.includes('MissingProfileReq')), 'RM-C5 missing request evidence missing.');
  ensure(evidence.some((line) => line.includes('dispatch-error') && line.includes('MissingProfileMsg')), 'RM-C5 missing send evidence missing.');
  const reply = await postJson<ProfileRes>(discoveryConsumerUrl, '/profile/request', { value: 'rm-c5-after' });
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
