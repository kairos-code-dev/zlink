import type {
  ActorFastReq,
  ActorJoinYieldReq,
  YieldEvidenceRes,
  YieldEvidenceReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { YieldActorScenarioContext } from './yd-b1-other-actor-progress-scenario';

export async function runYdB3(
  joinClient: ZlinkStreamConnector,
  fastClient: ZlinkStreamConnector,
  actors: YieldActorScenarioContext
): Promise<string> {
  const requestId = `YD-B3-${uniqueId()}`;
  const joining = joinClient
    .request({ requestId, targetSpotRid: actors.spotRid } satisfies ActorJoinYieldReq)
    .packetName('ActorJoinYieldReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  const fast = fastClient
    .request({ requestId, marker: 'b3-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  await Promise.all([joining, fast]);

  const evidence = await joinClient
    .request({ requestId } satisfies YieldEvidenceReq)
    .packetName('YieldEvidenceReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<YieldEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-join-yield-started',
    'actor-join-yield-released',
    'actor-fast-started',
    'actor-fast-completed',
    'actor-join-yield-resumed',
    'actor-join-yield-completed'
  ], 'YD-B3 marker order mismatch.');
  console.log('scenario YD-B3 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
