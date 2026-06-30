import type {
  EnsureSpotReply,
  EnsureSpotReq,
  ProbeCommand,
  YieldEvidenceReply,
  YieldEvidenceWaitReq,
  YieldTimeoutCommand
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdE1(client: ZlinkStreamConnector): Promise<void> {
  const spotRid = `yield-timeout-${uniqueId()}`;
  const spot = decodeStreamReply<EnsureSpotReply>(await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit());
  ensure(spot.spotRid === spotRid, 'YD-E1 spot creation mismatch.');

  const requestId = `YD-E1-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 700, timeoutMs: 100 } satisfies YieldTimeoutCommand)
    .packetName('YieldTimeoutCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'timeout-yield-completed');

  await client
    .send({ requestId, marker: 'timeout-probe' } satisfies ProbeCommand)
    .packetName('ProbeCommand')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  const evidence = await waitForEvidence(client, requestId, 'probe-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timeout-yield-started',
    'timeout-yield-released',
    'timeout-yield-completed',
    'probe-started',
    'probe-completed'
  ], 'YD-E1 timeout cleanup marker order mismatch.');
  ensure(
    evidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('marker=timeout-probe')),
    'YD-E1 post-timeout probe marker missing.'
  );

  await new Promise((resolve) => setTimeout(resolve, 750));
  const lateEvidence = await waitForEvidence(client, requestId, 'probe-completed');
  ensure(
    !lateEvidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('timeout-yield-unexpected-resumed')),
    'YD-E1 timeout call resumed after timeout.'
  );
  console.log('scenario YD-E1 passed');
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
