import type {
  ProbeMsg,
  YieldMsg,
  YieldEvidenceRes,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA2(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `YD-A2-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350, correlationId: 'corr-a2' } satisfies YieldMsg)
    .packetName('YieldMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await client
    .request({ requestId, marker: 'yield-released', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit();
  await client
    .send({ requestId, marker: 'yield-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'yield-completed', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<YieldEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'yield-started',
    'yield-released',
    'probe-started',
    'probe-completed',
    'yield-resumed',
    'yield-completed'
  ], 'YD-A2 marker order mismatch.');
  console.log('scenario YD-A2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
