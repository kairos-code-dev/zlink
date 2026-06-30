import type {
  ActorFastReq,
  ActorYieldReq,
  BindYieldActorsReply,
  BindYieldActorsReq,
  YieldEvidenceReply,
  YieldEvidenceReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export interface YieldActorScenarioContext {
  readonly spotRid: string;
  readonly actorA: string;
  readonly actorB: string;
}

export async function bindYieldActors(
  client: ZlinkStreamConnector,
  spotRid: string,
  existing?: Pick<YieldActorScenarioContext, 'actorA' | 'actorB'>,
  actorIds?: readonly string[]
): Promise<YieldActorScenarioContext> {
  const actorA = existing?.actorA ?? `yield-actor-a-${uniqueId()}`;
  const actorB = existing?.actorB ?? `yield-actor-b-${uniqueId()}`;
  const requestedActorIds = actorIds ?? [actorA, actorB];
  const reply = decodeStreamReply<BindYieldActorsReply>(await client
    .request({ spotRid, actorIds: requestedActorIds } satisfies BindYieldActorsReq)
    .packetName('BindYieldActorsReq')
    .timeout(30000)
    .submit());
  ensure(reply.actors.length === requestedActorIds.length, 'YD-B actor bind count mismatch.');
  return { spotRid: reply.spotRid, actorA, actorB };
}

export async function runYdB1(
  yieldClient: ZlinkStreamConnector,
  fastClient: ZlinkStreamConnector,
  actors: YieldActorScenarioContext
): Promise<string> {
  const requestId = `YD-B1-${uniqueId()}`;
  const yielding = yieldClient
    .request({ requestId, delayMs: 350 } satisfies ActorYieldReq)
    .packetName('ActorYieldReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  const fast = fastClient
    .request({ requestId, marker: 'b1-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  await Promise.all([yielding, fast]);

  const evidence = decodeStreamReply<YieldEvidenceReply>(await yieldClient
    .request({ requestId } satisfies YieldEvidenceReq)
    .packetName('YieldEvidenceReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-yield-started',
    'actor-yield-released',
    'actor-fast-started',
    'actor-fast-completed',
    'actor-yield-resumed',
    'actor-yield-completed'
  ], 'YD-B1 marker order mismatch.');
  console.log('scenario YD-B1 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
