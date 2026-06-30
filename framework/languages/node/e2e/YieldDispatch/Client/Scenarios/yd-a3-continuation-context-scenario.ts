import type {
  YieldMsg,
  YieldEvidenceRes,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA3(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `YD-A3-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 50, correlationId: 'corr-a3' } satisfies YieldMsg)
    .packetName('YieldMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = decodeStreamReply<YieldEvidenceRes>(await client
    .request({ requestId, marker: 'yield-completed', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'yield-started',
    'yield-released',
    'yield-resumed',
    'yield-completed'
  ], 'YD-A3 marker order mismatch.');
  console.log('scenario YD-A3 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
