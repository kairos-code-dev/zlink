import type {
  ActorPushNotify,
  ActorPushYieldReq,
  ActorYieldRes,
  YieldEvidenceRes,
  YieldEvidenceReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { YieldActorScenarioContext } from './yd-b1-other-actor-progress-scenario';

export async function runYdD4(
  client: ZlinkStreamConnector,
  createUnboundClient: () => ZlinkStreamConnector,
  actors: YieldActorScenarioContext
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
    const requestId = `YD-D4-${uniqueId()}`;
    const pushed = client.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === actors.actorA && message.payload.requestId === requestId)
      .timeout(30000)
      .submit();
    const reply = decodeStreamReply<ActorYieldRes>(await client
      .request({ requestId, delayMs: 250, value: 'bound-session-push' } satisfies ActorPushYieldReq)
      .packetName('ActorPushYieldReq')
      .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
      .timeout(30000)
      .submit());
    const notify = await pushed;
    ensure(reply.scenarioId === 'YD-D4', 'YD-D4 reply scenario mismatch.');
    ensure(notify.payload.actorId === actors.actorA, 'YD-D4 push actor mismatch.');
    ensure(notify.payload.requestId === requestId, 'YD-D4 push request mismatch.');
    ensure(notify.payload.value === 'bound-session-push', 'YD-D4 push value mismatch.');
    await new Promise((resolve) => setTimeout(resolve, 150));
    ensure(unboundPushCount === 0, 'YD-D4 unbound session received actor push.');

    const evidence = decodeStreamReply<YieldEvidenceRes>(await client
      .request({ requestId } satisfies YieldEvidenceReq)
      .packetName('YieldEvidenceReq')
      .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
      .timeout(30000)
      .submit());
    containsRequestMarkersInOrder(evidence.evidence, requestId, [
      'actor-push-yield-started',
      'actor-push-yield-released',
      'actor-push-yield-resumed',
      'actor-push-yield-completed'
    ], 'YD-D4 actor push yield marker order mismatch.');
  } finally {
    unboundSubscription.dispose();
    await unbound.close();
  }
  console.log('scenario YD-D4 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
