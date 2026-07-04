import type { ActorCallRequest, ActorCallResponse, ActorEvidence } from '../Shared/messages';

interface ClientOptions {
  readonly actorUrl: string;
  readonly callerUrl: string;
}

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));

  await post(`${options.actorUrl}/actors/ta-a1/ensure`, {});
  await assertCall(options, 'TA-A1-send', 'ta-a1', 'a1-send', 'sent', true);
  await assertCall(options, 'TA-A1-request', 'ta-a1', 'a1-request', 'reply:a1-request', false);

  await post(`${options.actorUrl}/actors/ta-a2/ensure`, {});
  await assertCall(options, 'TA-A2-unbound-send', 'ta-a2', 'a2-send', 'sent', true);
  await assertCall(options, 'TA-A2-unbound-request', 'ta-a2', 'a2-request', 'reply:a2-request', false);

  await assertFailure(options, 'TA-A3-before-bind', 'ta-a3', 'actorRouteNotFound', true);
  await post(`${options.actorUrl}/actors/ta-a3/ensure`, {});
  await assertCall(options, 'TA-A3-after-bind-send', 'ta-a3', 'a3-send', 'sent', true);
  await assertCall(options, 'TA-A3-after-bind-request', 'ta-a3', 'a3-request', 'reply:a3-request', false);

  await post(`${options.actorUrl}/actors/ta-a4/ensure`, {});
  await assertCall(options, 'TA-A4-disconnected-send', 'ta-a4', 'a4-send', 'sent', true);
  await assertCall(options, 'TA-A4-disconnected-request', 'ta-a4', 'a4-request', 'reply:a4-request', false);

  await assertFailure(options, 'TA-B1-missing', 'missing-actor', 'actorRouteNotFound', true);
  await assertFailure(options, 'TA-B1-missing-request', 'missing-actor', 'actorRouteNotFound', false);

  const evidence = await getJson<ActorEvidence[]>(`${options.actorUrl}/evidence`);
  requireEvidence(evidence, 'TA-A1-send', 'send');
  requireEvidence(evidence, 'TA-A1-request', 'request');
  requireEvidence(evidence, 'TA-A2-unbound-send', 'send');
  requireEvidence(evidence, 'TA-A3-after-bind-request', 'request');
  requireEvidence(evidence, 'TA-A4-disconnected-send', 'send');

  console.log('to-actor-messaging e2e result=passed');
}

async function assertCall(
  options: ClientOptions,
  scenario: string,
  actorId: string,
  value: string,
  expected: string,
  send: boolean
): Promise<void> {
  const response = await postJson<ActorCallResponse>(
    `${options.callerUrl}/${send ? 'send' : 'request'}`,
    { scenario, actorId, value } satisfies ActorCallRequest
  );
  requireCondition(response.result === expected, `${scenario} expected '${expected}', got '${response.result}'.`);
  requireCondition(response.errorKind === undefined, `${scenario} unexpected error '${response.errorKind}'.`);
}

async function assertFailure(
  options: ClientOptions,
  scenario: string,
  actorId: string,
  expectedKind: string,
  send: boolean
): Promise<void> {
  const response = await postJson<ActorCallResponse>(
    `${options.callerUrl}/${send ? 'send' : 'request'}`,
    { scenario, actorId, value: 'missing' } satisfies ActorCallRequest
  );
  requireCondition(response.errorKind === expectedKind, `${scenario} expected '${expectedKind}', got '${response.errorKind}'.`);
}

function requireEvidence(evidence: readonly ActorEvidence[], scenario: string, kind: string): void {
  requireCondition(evidence.some((item) => item.scenario === scenario && item.kind === kind), `${scenario} ${kind} evidence missing.`);
}

async function getJson<T>(url: string): Promise<T> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`${url} failed with ${response.status}`);
  }
  return await response.json() as T;
}

async function post(url: string, body: unknown): Promise<void> {
  await postJson<unknown>(url, body);
}

async function postJson<T>(url: string, body: unknown): Promise<T> {
  const response = await fetch(url, {
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
    callerUrl: values.get('caller-url') ?? 'http://127.0.0.1:0'
  };
}

function requireCondition(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
