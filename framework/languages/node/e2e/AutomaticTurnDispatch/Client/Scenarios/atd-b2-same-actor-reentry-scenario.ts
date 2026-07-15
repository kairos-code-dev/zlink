// ATD-B2: 한 actor의 await 중 같은 actor mailbox 재진입 방지 검증한다.
import type {
  ActorFastMsg,
  ActorAwaitReq,
  AwaitEvidenceRes,
  AwaitEvidenceReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { AwaitActorScenarioContext } from './atd-b1-other-actor-progress-scenario';

export async function runYdB2(
  client: ZlinkStreamConnector,
  actors: AwaitActorScenarioContext
): Promise<string> {
  const requestId = `ATD-B2-${uniqueId()}`;
  const awaiting = client
    .request({ requestId, delayMs: 350 } satisfies ActorAwaitReq)
    .packetName('ActorAwaitReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'b2-fast' } satisfies ActorFastMsg)
    .packetName('ActorFastMsg')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
    .submit();
  await awaiting;

  const evidence = await client
    .request({ requestId } satisfies AwaitEvidenceReq)
    .packetName('AwaitEvidenceReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'actor-await-started',
    'actor-await-released',
    'actor-await-resumed',
    'actor-await-completed',
    'actor-fast-started',
    'actor-fast-completed'
  ], 'ATD-B2 marker order mismatch.');
  console.log('scenario ATD-B2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
