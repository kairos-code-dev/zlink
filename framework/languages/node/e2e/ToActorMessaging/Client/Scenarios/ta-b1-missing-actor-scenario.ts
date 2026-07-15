// TA-B1: 없는 actor 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import {
  type ActorEvidence, assertCall, assertFailure, ensureActor, requireNoEvidence
} from '../Support/actor-scenario-support';

export async function runTaB1(options: ClientOptions): Promise<void> {
  const reference = await ensureActor(options, 'ta-b1-reference');
  await postJson(`${options.actorUrl}/actors/ta-b1-reference/destroy`, {});
  await assertCall(options, 'TA-B1-missing-send', 'ta-b1-reference', reference.actor, 'missing', 'sent', true);
  await assertFailure(options, 'TA-B1-missing-request', 'ta-b1-reference', 'actorRouteNotFound', false, reference.actor);
  requireNoEvidence(await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`), 'TA-B1-missing-send');
  console.log('scenario TA-B1 passed');
}
