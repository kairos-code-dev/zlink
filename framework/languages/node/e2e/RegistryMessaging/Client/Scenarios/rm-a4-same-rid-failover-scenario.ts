import type { ClientOptions } from '../Support/client-options';
import { DynamicClusterLauncher } from '../Support/dynamic-cluster-launcher';
import { getJson, postJson } from '../Support/http-client';
import { countNewEvidence, ensure, uniqueMarker } from '../Support/scenario-assert';
import type { ProfileReply } from '../../Shared/messages';

export async function runRmA4(options: ClientOptions): Promise<void> {
  const cluster = await DynamicClusterLauncher.start(options, 'rm-a4');
  try {
    const providerV1 = await cluster.startProvider('api-a-v1', 'api-a');
    const first = await postJson<ProfileReply>(providerV1.httpUrl, '/profile/request', { value: 'rm-a4-v1' });
    ensure(first.providerRid === 'api-a', 'RM-A4 initial request should reach api-a.');
    await postJson<string[]>(providerV1.httpUrl, '/evidence/wait', { contains: 'value=rm-a4-v1' });

    await cluster.stop(providerV1);
    const providerV2 = await cluster.startProvider('api-a-v2', 'api-a');
    const beforeV1 = await readEvidenceIgnoringStopped(providerV1.httpUrl);
    const beforeV2 = await getJson<string[]>(providerV2.httpUrl, '/evidence');
    const marker = uniqueMarker('rm-a4');
    for (let i = 0; i < 20; i += 1) {
      const reply = await postJson<ProfileReply>(providerV2.httpUrl, '/profile/request', { value: `${marker}-${i}` });
      ensure(reply.providerRid === 'api-a', 'RM-A4 replacement request should reach api-a.');
    }
    const afterV2 = await postJson<string[]>(providerV2.httpUrl, '/evidence/wait', { contains: `${marker}-19` });
    const afterV1 = await readEvidenceIgnoringStopped(providerV1.httpUrl);
    const v1Count = countNewEvidence(afterV1, beforeV1, 'profile-request|rid=api-a', marker);
    const v2Count = countNewEvidence(afterV2, beforeV2, 'profile-request|rid=api-a', marker);
    ensure(v1Count === 0 && v2Count === 20, 'RM-A4 replacement provider evidence did not match.');
    console.log('scenario RM-A4 passed');
  } finally {
    await cluster.close();
  }
}

async function readEvidenceIgnoringStopped(url: string): Promise<string[]> {
  try {
    return await getJson<string[]>(url, '/evidence');
  } catch {
    return [];
  }
}
