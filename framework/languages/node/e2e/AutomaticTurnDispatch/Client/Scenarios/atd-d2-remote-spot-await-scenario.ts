import type {
  EnsureSpotRes,
  EnsureSpotReq,
  RemoteSpotAwaitReq,
  AutomaticTurnDispatchRes,
  AwaitEvidenceRes,
  AwaitEvidenceReq,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdD2(client: ZlinkStreamConnector): Promise<void> {
  const ownerSpotRid = `await-remote-owner-${uniqueId()}`;
  const targetSpotRid = `await-remote-target-${uniqueId()}`;
  await client
    .request({ spotRid: ownerSpotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotRes>();
  await client
    .request({ spotRid: targetSpotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<EnsureSpotRes>();

  const requestId = `ATD-D2-${uniqueId()}`;
  const reply = await client
    .request({ requestId, targetSpotRid, delayMs: 350 } satisfies RemoteSpotAwaitReq)
    .packetName('RemoteSpotAwaitReq')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, ownerSpotRid)
    .timeout(30000)
    .submit<AutomaticTurnDispatchRes>();
  ensure(reply.scenarioId === 'ATD-D2', 'ATD-D2 reply scenario mismatch.');
  ensure(reply.nodeRid === 'play-a', 'ATD-D2 caller continuation node mismatch.');

  const ownerEvidence = await client
    .request({ requestId, marker: 'remote-await-completed' } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  ensure(ownerEvidence.evidence.some((line) =>
    line.includes('remote-await-resumed|rid=play-a') && line.includes('targetNode=play-b')),
  'ATD-D2 continuation did not return to the owner node.');
  containsRequestMarkersInOrder(ownerEvidence.evidence, requestId, [
    'remote-await-started',
    'remote-await-released',
    'remote-await-resumed',
    'remote-await-completed'
  ], 'ATD-D2 owner marker order mismatch.');

  const targetEvidence = await client
    .request({ requestId } satisfies AwaitEvidenceReq)
    .packetName('AwaitEvidenceReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  ensure(targetEvidence.evidence.some((line) =>
    line.includes(`await-started|rid=play-b|spot=${targetSpotRid}|request=${requestId}`)),
  'ATD-D2 target play-b marker missing.');
  ensure(targetEvidence.evidence.every((line) => !line.includes('remote-await-resumed|rid=play-b')),
    'ATD-D2 target node must not own the caller continuation.');
  console.log('scenario ATD-D2 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
