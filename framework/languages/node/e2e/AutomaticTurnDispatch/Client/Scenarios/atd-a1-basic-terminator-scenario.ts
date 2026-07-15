import type {
  EnsureSpotRes,
  EnsureSpotReq,
  HoldMsg,
  ProbeMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA1(client: ZlinkStreamConnector): Promise<{ spotRid: string; requestId: string }> {
  const spotRid = `await-track-a-${uniqueId()}`;
  const spot = await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotRes>();
  ensure(spot.spotRid === spotRid, 'ATD-A1 spot creation mismatch.');

  const requestId = `ATD-A1-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350 } satisfies HoldMsg)
    .packetName('HoldMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'hold-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'probe-completed', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'hold-started',
    'hold-resumed',
    'hold-completed',
    'probe-started',
    'probe-completed'
  ], 'ATD-A1 marker order mismatch.');
  console.log('scenario ATD-A1 passed');
  return { spotRid, requestId };
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
