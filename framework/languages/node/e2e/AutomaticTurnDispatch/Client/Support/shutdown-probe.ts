import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode,
  ZlinkStreamException
} from '@zlink-systems/stream-connector';
import type { AwaitScenarioRes, AwaitShutdownRecoveryReq, AwaitShutdownScenarioReq } from '../../Shared/messages';
import type { ClientOptions } from './client-options';
import { ensure } from './scenario-assert';

export async function runShutdownWait(options: ClientOptions): Promise<void> {
  const requestId = requireOption(options.requestId, 'request-id');
  const spotRid = requireOption(options.spotRid, 'spot-rid');
  const client = createClient(options.sessionAStreamEndpoint);
  await client.connect();
  try {
    const reply = await client.request({ requestId, spotRid, delayMs: 30000 } satisfies AwaitShutdownScenarioReq)
      .packetName('AwaitShutdownScenarioReq').timeout(90000).submit<AwaitScenarioRes>();
    throw new Error(`TD-F5 expected shutdown during a pending await, but '${reply.operation}' completed.`);
  } catch (error) {
    if (error instanceof ZlinkStreamException || isAbortLike(error)) {
      console.log('execution-turn shutdown wait result=passed');
      return;
    }
    throw error;
  } finally {
    await client.close();
  }
}

export async function runShutdownRecovery(options: ClientOptions): Promise<void> {
  const requestId = requireOption(options.requestId, 'request-id');
  const spotRid = requireOption(options.spotRid, 'spot-rid');
  const client = createClient(options.sessionAStreamEndpoint);
  await client.connect();
  try {
    const result = await client.request({ requestId, spotRid } satisfies AwaitShutdownRecoveryReq)
      .packetName('AwaitShutdownRecoveryReq').timeout(30000).submit<AwaitScenarioRes>();
    ensure(result.operation === 'await.e3-shutdown-recovery', 'TD-F5 recovery operation mismatch.');
    ensure(result.spotRid === spotRid, 'TD-F5 recovery Spot routing id mismatch.');
    ensure(result.evidence.some((line) => line.includes(`request=${requestId}`)
      && line.includes('marker=shutdown-recovery-probe')),
    'TD-F5 recovery probe marker missing.');
    console.log('execution-turn shutdown recovery result=passed');
  } finally {
    await client.close();
  }
}

function createClient(endpoint: string) {
  return zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 30000,
    requestTimeoutMs: 60000,
    maxReceivedMessages: 1024
  });
}

function requireOption(value: string | undefined, name: string): string {
  if (value === undefined || value.length === 0) throw new Error(`--${name} is required for shutdown probes.`);
  return value;
}

function isAbortLike(error: unknown): boolean {
  return error instanceof Error && /abort|cancel|close|closed|disconnect|terminated/i.test(error.message);
}
