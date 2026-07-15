// ATD-B3: actor join 대기 중 다른 actor mailbox 진행 검증한다.
import type {
  ActorFastReq,
  ActorJoinAwaitReq,
  AwaitEvidenceRes,
  AwaitEvidenceReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { AwaitActorScenarioContext } from './atd-b1-other-actor-progress-scenario';

export async function runYdB3(
  joinClient: ZlinkStreamConnector,
  fastClient: ZlinkStreamConnector,
  actors: AwaitActorScenarioContext
): Promise<string> {
  const requestId = `ATD-B3-${uniqueId()}`;
  const joining = joinClient
    .request({ requestId, targetSpotRid: actors.spotRid } satisfies ActorJoinAwaitReq)
    .packetName('ActorJoinAwaitReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  const fast = fastClient
    .request({ requestId, marker: 'b3-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  await Promise.all([joining, fast]);

  const evidence = await joinClient
    .request({ requestId } satisfies AwaitEvidenceReq)
    .packetName('AwaitEvidenceReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-join-await-started',
    'actor-join-await-released',
    'actor-fast-started',
    'actor-fast-completed',
    'actor-join-await-resumed',
    'actor-join-await-completed'
  ], 'ATD-B3 marker order mismatch.');
  console.log('scenario ATD-B3 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
