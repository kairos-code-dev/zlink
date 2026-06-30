import type {
  EnsureSpotReply,
  EnsureSpotReq,
  ProbeCommand,
  YieldCommand,
  YieldEvidenceReply,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdD3(client: ZlinkStreamConnector): Promise<void> {
  const requestId = `YD-D3-${uniqueId()}`;
  const spotRid = `yield-route-bridge-${uniqueId()}`;
  await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<EnsureSpotReply>();

  await client
    .send({ requestId, delayMs: 250, correlationId: 'route-bridge' } satisfies YieldCommand)
    .packetName('YieldCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'route-bridge-probe' } satisfies ProbeCommand)
    .packetName('ProbeCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId, marker: 'yield-completed' } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'yield-started',
    'yield-released',
    'probe-started',
    'probe-completed',
    'yield-resumed',
    'yield-completed'
  ], 'YD-D3 route bridge marker order mismatch.');
  ensure(evidence.evidence.some((line) =>
    line.includes('yield-started|rid=play-b') && line.includes('handler=spot')),
  'YD-D3 target Spot handler marker missing.');
  console.log('scenario YD-D3 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
