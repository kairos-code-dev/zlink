// SM-D4A: Session A의 stale binding과 late disconnect가 Session B rebind를 건드리지 않는지 검증한다.
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import {
  bindActor,
  countEvidence,
  createSessionClient,
  delay,
  expectStaleActorRoute,
  getEvidence,
  pingActor,
  waitEvidence
} from '../Support/session-binding-support';

export async function runSmD4A(options: ClientOptions): Promise<void> {
  const suffix = Date.now();
  const reboundActorId = `actor-sm-d4a-rebound-${suffix}`;
  const sessionAActorId = `actor-sm-d4a-session-a-${suffix}`;
  const sessionBActorId = `actor-sm-d4a-session-b-${suffix}`;
  const sessionA = createSessionClient(options.sessionAStreamEndpoint);
  const sessionB = createSessionClient(options.sessionBStreamEndpoint);

  await sessionA.connect();
  await sessionB.connect();
  try {
    const original = await bindActor(sessionA, reboundActorId, 'play-a');
    await bindActor(sessionA, sessionAActorId, 'play-a');
    const rebound = await bindActor(sessionB, reboundActorId, 'play-a');
    await bindActor(sessionB, sessionBActorId, 'play-b');
    ensure(
      original.generation === rebound.generation,
      'SM-D4A explicit rebind changed ObjectGeneration.'
    );

    await expectStaleActorRoute(sessionA, reboundActorId, 'stale-session-a');
    const current = await pingActor(sessionB, reboundActorId, 'current-session-b');
    const other = await pingActor(sessionB, sessionBActorId, 'session-b-other');
    ensure(current.actorId === reboundActorId, 'SM-D4A current binding relay mismatch.');
    ensure(other.actorId === sessionBActorId, 'SM-D4A Session B other Actor binding changed.');

    await sessionA.close();
    await waitEvidence(
      options.playAUrl,
      `entry-disconnected|rid=play-a|actor=${sessionAActorId}`
    );
    await delay(300);
    const playAEvidence = await getEvidence(options.playAUrl);
    ensure(
      countEvidence(
        playAEvidence,
        `entry-disconnected|rid=play-a|actor=${reboundActorId}`
      ) === 0,
      'SM-D4A Session A late disconnect reached the Session B current binding.'
    );

    const afterLateDisconnect = await pingActor(sessionB, reboundActorId, 'after-late-disconnect');
    ensure(
      afterLateDisconnect.actorId === reboundActorId,
      'SM-D4A current binding was removed by stale lifecycle cleanup.'
    );
  } finally {
    await Promise.allSettled([sessionA.close(), sessionB.close()]);
  }

  console.log('scenario SM-D4A passed');
}
