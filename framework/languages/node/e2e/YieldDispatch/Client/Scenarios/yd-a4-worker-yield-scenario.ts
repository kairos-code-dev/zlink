import type {
  ProbeCommand,
  WorkerYieldCommand,
  YieldEvidenceReply,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA4(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `YD-A4-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350 } satisfies WorkerYieldCommand)
    .packetName('WorkerYieldCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await client
    .request({ requestId, marker: 'worker-yield-released', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit();
  await client
    .send({ requestId, marker: 'worker-probe' } satisfies ProbeCommand)
    .packetName('ProbeCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId, marker: 'worker-yield-completed', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'worker-yield-started',
    'worker-yield-released',
    'probe-started',
    'probe-completed',
    'worker-yield-resumed',
    'worker-yield-completed'
  ], 'YD-A4 marker order mismatch.');
  console.log('scenario YD-A4 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
