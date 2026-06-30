import type {
  EnsureSpotReply,
  EnsureSpotReq,
  RemoteSpotYieldReq,
  YieldDispatchReply,
  YieldEvidenceReply,
  YieldEvidenceReq,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdD2(client: ZlinkStreamConnector): Promise<void> {
  const ownerSpotRid = `yield-remote-owner-${uniqueId()}`;
  const targetSpotRid = `yield-remote-target-${uniqueId()}`;
  await client
    .request({ spotRid: ownerSpotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotReply>();
  await client
    .request({ spotRid: targetSpotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<EnsureSpotReply>();

  const requestId = `YD-D2-${uniqueId()}`;
  const reply = decodeStreamReply<YieldDispatchReply>(await client
    .request({ requestId, targetSpotRid, delayMs: 350 } satisfies RemoteSpotYieldReq)
    .packetName('RemoteSpotYieldReq')
    .metadata(YieldDispatchNames.spotRidMetadata, ownerSpotRid)
    .timeout(30000)
    .submit());
  ensure(reply.scenarioId === 'YD-D2', 'YD-D2 reply scenario mismatch.');
  ensure(reply.nodeRid === 'play-a', 'YD-D2 caller continuation node mismatch.');

  const ownerEvidence = decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId, marker: 'remote-yield-completed' } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  ensure(ownerEvidence.evidence.some((line) =>
    line.includes('remote-yield-resumed|rid=play-a') && line.includes('targetNode=play-b')),
  'YD-D2 continuation did not return to the owner node.');
  containsRequestMarkersInOrder(ownerEvidence.evidence, requestId, [
    'remote-yield-started',
    'remote-yield-released',
    'remote-yield-resumed',
    'remote-yield-completed'
  ], 'YD-D2 owner marker order mismatch.');

  const targetEvidence = decodeStreamReply<YieldEvidenceReply>(await client
    .request({ requestId } satisfies YieldEvidenceReq)
    .packetName('YieldEvidenceReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit());
  ensure(targetEvidence.evidence.some((line) =>
    line.includes(`yield-started|rid=play-b|spot=${targetSpotRid}|request=${requestId}`)),
  'YD-D2 target play-b marker missing.');
  ensure(targetEvidence.evidence.every((line) => !line.includes('remote-yield-resumed|rid=play-b')),
    'YD-D2 target node must not own the caller continuation.');
  console.log('scenario YD-D2 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
