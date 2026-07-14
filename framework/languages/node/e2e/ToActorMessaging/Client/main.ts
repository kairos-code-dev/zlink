import {
  PacketNames,
  type ActorCallRequest,
  type ActorCallResponse,
  type ActorEvidence,
  type ActorEnsureResponse,
  type ActorPushNotify,
  type ActorRefSnapshot,
  type BindActorReq,
  type BindActorRes
} from '../Shared/messages';
import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode,
  type ZlinkStreamConnector
} from '@zlink-systems/stream-connector';
import { browserE2eArgs, browserE2eFetch, runBrowserE2e } from '../../browser-client-runtime';

interface ClientOptions {
  readonly actorUrl: string;
  readonly callerUrl: string;
  readonly sessionStreamEndpoint: string;
  readonly scenario: string;
}

async function main(): Promise<void> {
  const options = parseClientOptions(browserE2eArgs());
  const scenarios: Record<string, (options: ClientOptions) => Promise<void>> = {
    'TA-A1': runTaA1,
    'TA-A2': runTaA2,
    'TA-A3': runTaA3,
    'TA-A4': runTaA4,
    'TA-B1': runTaB1,
    'TA-B2': runTaB2,
    'TA-B3': runTaB3
  };

  if (options.scenario === 'all') {
    for (const scenario of Object.values(scenarios)) {
      await scenario(options);
    }
  } else {
    const scenario = scenarios[options.scenario];
    if (scenario === undefined) {
      throw new Error(`Unknown scenario '${options.scenario}'.`);
    }
    await scenario(options);
  }

  console.log('to-actor-messaging e2e result=passed');
}

async function runTaA1(options: ClientOptions): Promise<void> {
  const taA1 = await ensureActor(options, 'ta-a1');
  const taA1Session = await bindActor(options, taA1.actor);
  try {
    await assertBoundPush(taA1Session, 'TA-A1-push-before-no-bind', 'ta-a1', 'a1-push-before');
    await assertCall(options, 'TA-A1-send', 'ta-a1', taA1.actor, 'a1-send', 'sent', true);
    await assertCall(options, 'TA-A1-request', 'ta-a1', taA1.actor, 'a1-request', 'reply:a1-request', false);
    await assertBoundPush(taA1Session, 'TA-A1-push-after-no-bind', 'ta-a1', 'a1-push-after');
  } finally {
    await taA1Session.close();
  }
  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireEvidence(evidence, 'TA-A1-send', 'send');
  requireEvidence(evidence, 'TA-A1-request', 'request');
  requireEvidence(evidence, 'TA-A1-push-before-no-bind', 'push');
  requireEvidence(evidence, 'TA-A1-push-after-no-bind', 'push');
  console.log('scenario TA-A1 passed');
}

async function runTaA2(options: ClientOptions): Promise<void> {
  const taA2 = await ensureActor(options, 'ta-a2');
  await assertCall(options, 'TA-A2-unbound-send', 'ta-a2', taA2.actor, 'a2-send', 'sent', true);
  await assertCall(options, 'TA-A2-unbound-request', 'ta-a2', taA2.actor, 'a2-request', 'reply:a2-request', false);
  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireEvidence(evidence, 'TA-A2-unbound-send', 'send');
  requireEvidence(evidence, 'TA-A2-unbound-request', 'request');
  console.log('scenario TA-A2 passed');
}

async function runTaA3(options: ClientOptions): Promise<void> {
  const taA3 = await ensureActor(options, 'ta-a3');
  await assertCall(options, 'TA-A3-before-bind-send', 'ta-a3', taA3.actor, 'a3-before-send', 'sent', true);
  await assertCall(options, 'TA-A3-before-bind-request', 'ta-a3', taA3.actor, 'a3-before-request', 'reply:a3-before-request', false);
  const taA3Session = await bindActor(options, taA3.actor);
  try {
    await assertCall(options, 'TA-A3-after-bind-send', 'ta-a3', taA3.actor, 'a3-send', 'sent', true);
    await assertCall(options, 'TA-A3-after-bind-request', 'ta-a3', taA3.actor, 'a3-request', 'reply:a3-request', false);
    await assertBoundPush(taA3Session, 'TA-A3-after-bind-push', 'ta-a3', 'a3-push');
  } finally {
    await taA3Session.close();
  }
  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireEvidence(evidence, 'TA-A3-before-bind-send', 'send');
  requireEvidence(evidence, 'TA-A3-before-bind-request', 'request');
  requireEvidence(evidence, 'TA-A3-after-bind-send', 'send');
  requireEvidence(evidence, 'TA-A3-after-bind-request', 'request');
  requireEvidence(evidence, 'TA-A3-after-bind-push', 'push');
  console.log('scenario TA-A3 passed');
}

async function runTaA4(options: ClientOptions): Promise<void> {
  const taA4 = await ensureActor(options, 'ta-a4');
  const taA4Session = await bindActor(options, taA4.actor);
  await taA4Session.close();
  await assertCall(options, 'TA-A4-disconnected-send', 'ta-a4', taA4.actor, 'a4-send', 'sent', true);
  await assertCall(options, 'TA-A4-disconnected-request', 'ta-a4', taA4.actor, 'a4-request', 'reply:a4-request', false);
  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireEvidence(evidence, 'TA-A4-disconnected-send', 'send');
  requireEvidence(evidence, 'TA-A4-disconnected-request', 'request');
  console.log('scenario TA-A4 passed');
}

async function runTaB1(options: ClientOptions): Promise<void> {
  const reference = await ensureActor(options, 'ta-b1-reference');
  await postJson(`${options.actorUrl}/actors/ta-b1-reference/destroy`, {});
  const missingActor = reference.actor;
  await assertCall(options, 'TA-B1-missing-send', 'ta-b1-reference', missingActor, 'missing', 'sent', true);
  await assertFailure(
    options,
    'TA-B1-missing-request',
    'ta-b1-reference',
    'actorRouteNotFound',
    false,
    missingActor
  );
  requireNoEvidence(
    await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`),
    'TA-B1-missing-send'
  );
  console.log('scenario TA-B1 passed');
}

async function runTaB2(options: ClientOptions): Promise<void> {
  const taB2 = await ensureActor(options, 'ta-b2');
  const staleActor = { ...taB2.actor, generation: (BigInt(taB2.actor.generation) + 1n).toString() };
  await assertCall(options, 'TA-B2-stale-send', 'ta-b2', staleActor, 'stale-send', 'sent', true);
  await assertFailure(options, 'TA-B2-stale-request', 'ta-b2', 'actorLocationStale', false, staleActor);
  requireNoEvidence(await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`), 'TA-B2-stale-send');
  console.log('scenario TA-B2 passed');
}

async function runTaB3(options: ClientOptions): Promise<void> {
  const taB3 = await ensureActor(options, 'ta-b3');
  const disconnectedRouteActor = { ...taB3.actor, nodeRid: 'to-actor-missing-route' };
  await assertCall(options, 'TA-B3-route-not-connected-send', 'ta-b3', disconnectedRouteActor, 'route-send', 'sent', true);
  await assertFailure(options, 'TA-B3-route-not-connected-request', 'ta-b3', 'routeNotConnected', false, disconnectedRouteActor);
  requireNoEvidence(
    await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`),
    'TA-B3-route-not-connected-send'
  );
  console.log('scenario TA-B3 passed');
}

async function bindActor(options: ClientOptions, actor: ActorRefSnapshot): Promise<ZlinkStreamConnector> {
  const client = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
  await client.connect();
  const reply = await client
    .request({ actor } satisfies BindActorReq)
    .packetName(PacketNames.bindActor)
    .timeout(5000)
    .submit<BindActorRes>();
  requireCondition(reply.actorId === actor.actorId, `bind ${actor.actorId} actor mismatch.`);
  requireCondition(reply.nodeRid === actor.nodeRid, `bind ${actor.actorId} node mismatch.`);
  requireCondition(reply.generation === actor.generation, `bind ${actor.actorId} generation mismatch.`);
  requireCondition(reply.boundCount >= 1, `bind ${actor.actorId} did not bind any actor.`);
  return client;
}

async function assertBoundPush(
  client: ZlinkStreamConnector,
  scenario: string,
  actorId: string,
  value: string
): Promise<void> {
  const pushed = client.waitFor<ActorPushNotify>(PacketNames.actorPush)
    .where((message) => message.payload.scenario === scenario && message.payload.actorId === actorId)
    .timeout(10000)
    .submit();
  const reply = await client
    .request({ scenario, actorId, value })
    .packetName(PacketNames.actorPush)
    .timeout(5000)
    .submit<{ readonly value: string }>();
  const notify = await pushed;
  requireCondition(reply.value === `pushed:${value}`, `${scenario} reply mismatch.`);
  requireCondition(notify.payload.value === value, `${scenario} push value mismatch.`);
}

async function assertCall(
  options: ClientOptions,
  scenario: string,
  actorId: string,
  actor: ActorRefSnapshot,
  value: string,
  expected: string,
  send: boolean,
  operation?: 'send' | 'request' | 'push'
): Promise<void> {
  const response = await postJson<ActorCallResponse>(
    `${options.callerUrl}/${operation ?? (send ? 'send' : 'request')}`,
    { scenario, actorId, actor, value } satisfies ActorCallRequest
  );
  requireCondition(response.result === expected, `${scenario} expected '${expected}', got '${response.result}'.`);
  requireCondition(response.errorKind === undefined, `${scenario} unexpected error '${response.errorKind}'.`);
}

async function ensureActor(options: ClientOptions, actorId: string): Promise<ActorEnsureResponse> {
  const response = await postJson<ActorEnsureResponse>(`${options.actorUrl}/actors/${actorId}/ensure`, {});
  assertConcreteActorRef(response.actor);
  return response;
}

function assertConcreteActorRef(actor: ActorRefSnapshot): void {
  requireCondition(actor.nodeRid.trim().length > 0, `Actor '${actor.actorId}' node rid is empty.`);
  requireCondition(BigInt(actor.generation) > 0n, `Actor '${actor.actorId}' generation is not positive.`);
}

async function assertFailure(
  options: ClientOptions,
  scenario: string,
  actorId: string,
  expectedKind: string,
  send: boolean,
  actor?: ActorRefSnapshot
): Promise<void> {
  const response = await postJson<ActorCallResponse>(
    `${options.callerUrl}/${send ? 'send' : 'request'}`,
    { scenario, actorId, actor, value: 'missing' } satisfies ActorCallRequest
  );
  requireCondition(response.errorKind === expectedKind, `${scenario} expected '${expectedKind}', got '${response.errorKind}'.`);
}

function requireEvidence(evidence: readonly ActorEvidence[], scenario: string, kind: string): void {
  requireCondition(evidence.some((item) => item.scenario === scenario && item.kind === kind), `${scenario} ${kind} evidence missing.`);
}

function requireNoEvidence(evidence: readonly ActorEvidence[], scenario: string): void {
  requireCondition(
    evidence.every((item) => item.scenario !== scenario),
    `${scenario} unexpectedly reached an actor handler.`
  );
}

async function getJson<T>(url: string): Promise<T> {
  const response = await browserE2eFetch(url);
  if (!response.ok) {
    throw new Error(`${url} failed with ${response.status}`);
  }
  return await response.json() as T;
}

async function postJson<T>(url: string, body: unknown): Promise<T> {
  const response = await browserE2eFetch(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!response.ok) {
    throw new Error(`${url} failed with ${response.status}`);
  }
  return await response.json() as T;
}

function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index]?.replace(/^--/, '');
    const value = args[index + 1];
    if (key === undefined || value === undefined) {
      throw new Error(`Missing value for '${args[index]}'.`);
    }
    values.set(key, value);
  }
  return {
    actorUrl: values.get('actor-url') ?? 'http://127.0.0.1:0',
    callerUrl: values.get('caller-url') ?? 'http://127.0.0.1:0',
    sessionStreamEndpoint: values.get('session-stream-endpoint') ?? 'ws://127.0.0.1:0',
    scenario: values.get('scenario') ?? 'all'
  };
}

function requireCondition(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
}

void runBrowserE2e('ToActorMessaging', main);
