import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode,
  ZlinkStreamException
} from '@zlink-systems/stream-connector';
import type { YieldScenarioRes, YieldShutdownRecoveryReq, YieldShutdownScenarioReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';

export async function runShutdownWait(options: ClientOptions): Promise<void> {
  const requestId = requireOption(options.requestId, 'request-id');
  const spotRid = requireOption(options.spotRid, 'spot-rid');
  const client = createClient(options.sessionAStreamEndpoint);
  await client.connect();
  try {
    const reply = await client
      .request({ requestId, spotRid, delayMs: 30000 } satisfies YieldShutdownScenarioReq)
      .packetName('YieldShutdownScenarioReq')
      .timeout(90000)
      .submit<YieldScenarioRes>();
    throw new Error(`YD-E3 expected play-a shutdown while yield was pending, but request completed as ${reply.operation}.`);
  } catch (error) {
    if (error instanceof ZlinkStreamException || isAbortLike(error)) {
      console.log('yield-dispatch shutdown wait result=passed');
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
    const result = await client
      .request({ requestId, spotRid } satisfies YieldShutdownRecoveryReq)
      .packetName('YieldShutdownRecoveryReq')
      .timeout(30000)
      .submit<YieldScenarioRes>();
    ensure(result.operation === 'yield.e3-shutdown-recovery', 'YD-E3 recovery operation mismatch.');
    ensure(result.spotRid === spotRid, 'YD-E3 recovery spot rid mismatch.');
    ensure(
      result.evidence.some((line) =>
        line.includes(`request=${requestId}`)
        && line.includes('marker=shutdown-recovery-probe')
      ),
      'YD-E3 recovery probe marker missing.'
    );
    console.log('yield-dispatch shutdown recovery result=passed');
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
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required for shutdown scenarios.`);
  }
  return value;
}

function isAbortLike(error: unknown): boolean {
  return error instanceof Error && /abort|cancel|close|closed|disconnect|terminated/i.test(error.message);
}
