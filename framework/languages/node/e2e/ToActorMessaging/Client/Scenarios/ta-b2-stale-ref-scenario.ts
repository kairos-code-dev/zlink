// TA-B2: stale actor ref 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { getJson } from '../Support/http-client';
import {
  type ActorEvidence, assertCall, assertFailure, ensureActor, requireNoEvidence
} from '../Support/actor-scenario-support';

export async function runTaB2(options: ClientOptions): Promise<void> {
  const actor = await ensureActor(options, 'ta-b2');
  const stale = { ...actor.actor, generation: (BigInt(actor.actor.generation) + 1n).toString() };
  await assertCall(options, 'TA-B2-stale-send', 'ta-b2', stale, 'stale-send', 'sent', true);
  await assertFailure(options, 'TA-B2-stale-request', 'ta-b2', 'actorLocationStale', false, stale);
  requireNoEvidence(await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`), 'TA-B2-stale-send');
  console.log('scenario TA-B2 passed');
}
