// SM-D5: physical session disconnect의 Framework 자동 Actor 통지를 검증한다.
import {
  bindActor,
  createSessionClient,
  pingActor,
  waitEvidence
} from '../Support/session-binding-support';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';

export async function runSmD5(options: ClientOptions): Promise<void> {
  const suffix = Date.now();
  const localActorId = `actor-sm-d5-local-${suffix}`;
  const failingActorId = `actor-sm-d5-fail-${suffix}`;
  const remoteActorId = `actor-sm-d5-remote-${suffix}`;
  const client = createSessionClient(options.sessionAStreamEndpoint);
  await client.connect();
  const local = await bindActor(client, localActorId, 'session-a');
  const failing = await bindActor(client, failingActorId, 'play-a');
  const remote = await bindActor(client, remoteActorId, 'play-b');
  try {
    ensure(
      local.generation !== undefined
      && failing.generation !== undefined
      && remote.generation !== undefined,
      'SM-D5 bind did not return exact Actor generations.'
    );
  } finally {
    await client.close();
  }

  const localEvidence = await waitEvidence(
    options.sessionAUrl,
    `entry-disconnected|rid=session-a|actor=${localActorId}`,
    'session-disconnected|rid=session-a|'
  );
  const failedEvidence = await waitEvidence(
    options.playAUrl,
    `entry-disconnected|rid=play-a|actor=${failingActorId}`
  );
  const remoteEvidence = await waitEvidence(
    options.playBUrl,
    `entry-disconnected|rid=play-b|actor=${remoteActorId}`
  );
  ensure(
    localEvidence.length > 0 && failedEvidence.length > 0 && remoteEvidence.length > 0,
    'SM-D5 automatic all-settled fan-out did not reach every captured Actor.'
  );

  const rebound = createSessionClient(options.sessionBStreamEndpoint);
  await rebound.connect();
  try {
    const current = await bindActor(rebound, remoteActorId, 'play-b');
    ensure(
      current.generation === remote.generation,
      'SM-D5 physical disconnect changed Actor ObjectGeneration.'
    );
    const reply = await pingActor(rebound, remoteActorId, 'after-physical-cleanup');
    ensure(reply.actorId === remoteActorId, 'SM-D5 callback failure prevented binding cleanup or rebind.');
  } finally {
    await rebound.close();
  }

  console.log('scenario SM-D5 passed');
}
