// TA-B2: resolve 뒤 generation 교체 시나리오를 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import {
  type ActorEvidence, assertCall, assertFailure, ensureActor, requireEvidence, requireNoEvidence
} from '../Support/actor-scenario-support';

export async function runTaB2(options: ClientOptions): Promise<void> {
  const previous = await ensureActor(options, 'ta-b2');
  await postJson(`${options.actorUrl}/actors/ta-b2/destroy`, {});
  const replacement = await ensureActor(options, 'ta-b2');
  if (replacement.actor.objectGeneration === previous.actor.objectGeneration) {
    throw new Error('TA-B2 actor recreation did not change object generation.');
  }

  await assertCall(options, 'TA-B2-stale-send', 'ta-b2', previous.actor, 'stale-send', 'sent', true);
  await assertFailure(options, 'TA-B2-stale-request', 'ta-b2', 'actorLocationStale', false, previous.actor);
  await assertCall(options, 'TA-B2-live-request', 'ta-b2', replacement.actor, 'live', 'reply:live', false);
  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireNoEvidence(evidence, 'TA-B2-stale-send');
  requireNoEvidence(evidence, 'TA-B2-stale-request');
  requireEvidence(evidence, 'TA-B2-live-request', 'request');
  console.log('scenario TA-B2 passed');
}
