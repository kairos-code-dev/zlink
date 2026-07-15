// ATD-B1: 한 actor의 await 중 다른 actor mailbox 진행 검증한다.
import type {
  ActorFastReq,
  ActorAwaitReq,
  BindAwaitActorsRes,
  BindAwaitActorsReq,
  AwaitEvidenceRes,
  AwaitEvidenceReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export interface AwaitActorScenarioContext {
  readonly spotRid: string;
  readonly actorA: string;
  readonly actorB: string;
}

export async function bindAwaitActors(
  client: ZlinkStreamConnector,
  spotRid: string,
  existing?: Pick<AwaitActorScenarioContext, 'actorA' | 'actorB'>,
  actorIds?: readonly string[]
): Promise<AwaitActorScenarioContext> {
  const actorA = existing?.actorA ?? `await-actor-a-${uniqueId()}`;
  const actorB = existing?.actorB ?? `await-actor-b-${uniqueId()}`;
  const requestedActorIds = actorIds ?? [actorA, actorB];
  const reply = await client
    .request({ spotRid, actorIds: requestedActorIds } satisfies BindAwaitActorsReq)
    .packetName('BindAwaitActorsReq')
    .timeout(30000)
    .submit<BindAwaitActorsRes>();
  ensure(reply.actors.length === requestedActorIds.length, 'ATD-B actor bind count mismatch.');
  return { spotRid: reply.spotRid, actorA, actorB };
}

export async function runYdB1(
  awaitClient: ZlinkStreamConnector,
  fastClient: ZlinkStreamConnector,
  actors: AwaitActorScenarioContext
): Promise<string> {
  const requestId = `ATD-B1-${uniqueId()}`;
  const awaiting = awaitClient
    .request({ requestId, delayMs: 350 } satisfies ActorAwaitReq)
    .packetName('ActorAwaitReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  const fast = fastClient
    .request({ requestId, marker: 'b1-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  await Promise.all([awaiting, fast]);

  const evidence = await awaitClient
    .request({ requestId } satisfies AwaitEvidenceReq)
    .packetName('AwaitEvidenceReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-await-started',
    'actor-await-released',
    'actor-fast-started',
    'actor-fast-completed',
    'actor-await-resumed',
    'actor-await-completed'
  ], 'ATD-B1 marker order mismatch.');
  console.log('scenario ATD-B1 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
