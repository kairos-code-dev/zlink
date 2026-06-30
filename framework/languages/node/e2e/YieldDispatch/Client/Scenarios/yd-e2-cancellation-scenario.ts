import type {
  EnsureSpotReply,
  EnsureSpotReq,
  ProbeCommand,
  YieldCancelCommand,
  YieldEvidenceReply,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdE2(client: ZlinkStreamConnector): Promise<void> {
  const spotRid = `yield-cancel-${uniqueId()}`;
  const spot = decodeStreamReply<EnsureSpotReply>(await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit());
  ensure(spot.spotRid === spotRid, 'YD-E2 spot creation mismatch.');

  const requestId = `YD-E2-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 800, cancelAfterMs: 100 } satisfies YieldCancelCommand)
    .packetName('YieldCancelCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'cancel-yield-completed');

  await client
    .send({ requestId, marker: 'cancel-probe' } satisfies ProbeCommand)
    .packetName('ProbeCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  const evidence = await waitForEvidence(client, requestId, 'probe-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'cancel-yield-started',
    'cancel-yield-released',
    'cancel-yield-completed',
    'probe-started',
    'probe-completed'
  ], 'YD-E2 cancellation cleanup marker order mismatch.');
  ensure(
    evidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('marker=cancel-probe')),
    'YD-E2 post-cancel probe marker missing.'
  );

  await new Promise((resolve) => setTimeout(resolve, 850));
  const lateEvidence = await waitForEvidence(client, requestId, 'probe-completed');
  ensure(
    !lateEvidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('cancel-yield-unexpected-resumed')),
    'YD-E2 canceled call resumed after cancellation.'
  );
  console.log('scenario YD-E2 passed');
}

async function waitForEvidence(
  client: ZlinkStreamConnector,
  requestId: string,
  marker: string
): Promise<YieldEvidenceReply> {
  return decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId, marker, timeoutMilliseconds: 30000 } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
