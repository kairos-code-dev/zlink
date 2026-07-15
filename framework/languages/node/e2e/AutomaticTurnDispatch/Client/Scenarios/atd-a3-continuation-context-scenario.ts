// ATD-A3: await 전후 request와 correlation 문맥 보존 검증한다.
import type {
  AwaitMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA3(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `ATD-A3-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 50, correlationId: 'corr-a3' } satisfies AwaitMsg)
    .packetName('AwaitMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'await-completed', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'await-started',
    'await-released',
    'await-resumed',
    'await-completed'
  ], 'ATD-A3 marker order mismatch.');
  console.log('scenario ATD-A3 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
