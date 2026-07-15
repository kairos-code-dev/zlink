// TA-B3: route 미연결 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { getJson } from '../Support/http-client';
import {
  type ActorEvidence, assertCall, assertFailure, ensureActor, requireNoEvidence
} from '../Support/actor-scenario-support';

export async function runTaB3(options: ClientOptions): Promise<void> {
  const actor = await ensureActor(options, 'ta-b3');
  const disconnected = { ...actor.actor, nodeRid: 'to-actor-missing-route' };
  await assertCall(options, 'TA-B3-route-not-connected-send', 'ta-b3', disconnected, 'route-send', 'sent', true);
  await assertFailure(options, 'TA-B3-route-not-connected-request', 'ta-b3', 'routeNotConnected', false, disconnected);
  requireNoEvidence(await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`), 'TA-B3-route-not-connected-send');
  console.log('scenario TA-B3 passed');
}
