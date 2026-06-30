import type {
  EnsureSpotReply,
  EnsureSpotReq,
  HoldCommand,
  ProbeCommand,
  YieldEvidenceReply,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA1(client: ZlinkStreamConnector): Promise<{ spotRid: string; requestId: string }> {
  const spotRid = `yield-track-a-${uniqueId()}`;
  const spot = decodeStreamReply<EnsureSpotReply>(await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit());
  ensure(spot.spotRid === spotRid, 'YD-A1 spot creation mismatch.');

  const requestId = `YD-A1-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350 } satisfies HoldCommand)
    .packetName('HoldCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'hold-probe' } satisfies ProbeCommand)
    .packetName('ProbeCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId, marker: 'probe-completed', timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'hold-started',
    'hold-resumed',
    'hold-completed',
    'probe-started'
  ], 'YD-A1 marker order mismatch.');
  console.log('scenario YD-A1 passed');
  return { spotRid, requestId };
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
