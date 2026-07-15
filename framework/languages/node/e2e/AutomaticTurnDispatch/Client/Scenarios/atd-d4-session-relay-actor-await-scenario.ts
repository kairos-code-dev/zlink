// ATD-D4: session relay actor await와 bound-session push 격리 검증한다.
import type {
  ActorPushNotify,
  ActorPushAwaitReq,
  ActorAwaitRes,
  AwaitEvidenceRes,
  AwaitEvidenceReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { AwaitActorScenarioContext } from './atd-b1-other-actor-progress-scenario';

export async function runYdD4(
  client: ZlinkStreamConnector,
  createUnboundClient: () => ZlinkStreamConnector,
  actors: AwaitActorScenarioContext
): Promise<void> {
  const unbound = createUnboundClient();
  let unboundPushCount = 0;
  const unboundSubscription = unbound.on<ActorPushNotify>('ActorPushNotify', (message) => {
    if (message.payload.actorId === actors.actorA) {
      unboundPushCount += 1;
    }
  });
  await unbound.connect();
  try {
    const requestId = `ATD-D4-${uniqueId()}`;
    const pushed = client.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === actors.actorA && message.payload.requestId === requestId)
      .timeout(30000)
      .submit();
    const reply = await client
      .request({ requestId, delayMs: 250, value: 'bound-session-push' } satisfies ActorPushAwaitReq)
      .packetName('ActorPushAwaitReq')
      .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
      .timeout(30000)
      .submit<ActorAwaitRes>();
    const notify = await pushed;
    ensure(reply.scenarioId === 'ATD-D4', 'ATD-D4 reply scenario mismatch.');
    ensure(notify.payload.actorId === actors.actorA, 'ATD-D4 push actor mismatch.');
    ensure(notify.payload.requestId === requestId, 'ATD-D4 push request mismatch.');
    ensure(notify.payload.value === 'bound-session-push', 'ATD-D4 push value mismatch.');
    await new Promise((resolve) => setTimeout(resolve, 150));
    ensure(unboundPushCount === 0, 'ATD-D4 unbound session received actor push.');

    const evidence = await client
      .request({ requestId } satisfies AwaitEvidenceReq)
      .packetName('AwaitEvidenceReq')
      .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
      .timeout(30000)
      .submit<AwaitEvidenceRes>();
    containsRequestMarkersInOrder(evidence.evidence, requestId, [
      'actor-push-await-started',
      'actor-push-await-released',
      'actor-push-await-resumed',
      'actor-push-await-completed'
    ], 'ATD-D4 actor push await marker order mismatch.');
  } finally {
    unboundSubscription.dispose();
    await unbound.close();
  }
  console.log('scenario ATD-D4 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
