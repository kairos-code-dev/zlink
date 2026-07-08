import type {
  ActorFastMsg,
  ActorYieldReq,
  YieldEvidenceRes,
  YieldEvidenceReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { YieldActorScenarioContext } from './yd-b1-other-actor-progress-scenario';

export async function runYdB2(
  client: ZlinkStreamConnector,
  actors: YieldActorScenarioContext
): Promise<string> {
  const requestId = `YD-B2-${uniqueId()}`;
  const yielding = client
    .request({ requestId, delayMs: 350 } satisfies ActorYieldReq)
    .packetName('ActorYieldReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'b2-fast' } satisfies ActorFastMsg)
    .packetName('ActorFastMsg')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
    .submit();
  await yielding;

  const evidence = await client
    .request({ requestId } satisfies YieldEvidenceReq)
    .packetName('YieldEvidenceReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<YieldEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-yield-started',
    'actor-yield-released',
    'actor-yield-resumed',
    'actor-yield-completed',
    'actor-fast-started',
    'actor-fast-completed'
  ], 'YD-B2 marker order mismatch.');
  console.log('scenario YD-B2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
