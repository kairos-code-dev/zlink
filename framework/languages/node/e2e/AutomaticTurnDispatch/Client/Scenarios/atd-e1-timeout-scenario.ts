// ATD-E1: await timeout 뒤 같은 Spot의 후속 처리 복구 검증한다.
import type {
  EnsureSpotRes,
  EnsureSpotReq,
  ProbeMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq,
  AwaitTimeoutMsg
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdE1(client: ZlinkStreamConnector): Promise<void> {
  const spotRid = `await-timeout-${uniqueId()}`;
  const spot = await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotRes>();
  ensure(spot.spotRid === spotRid, 'ATD-E1 spot creation mismatch.');

  const requestId = `ATD-E1-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 700, timeoutMs: 100 } satisfies AwaitTimeoutMsg)
    .packetName('AwaitTimeoutMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'timeout-await-completed');

  await client
    .send({ requestId, marker: 'timeout-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  const evidence = await waitForEvidence(client, requestId, 'probe-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timeout-await-started',
    'timeout-await-released',
    'timeout-await-completed',
    'probe-started',
    'probe-completed'
  ], 'ATD-E1 timeout cleanup marker order mismatch.');
  ensure(
    evidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('marker=timeout-probe')),
    'ATD-E1 post-timeout probe marker missing.'
  );

  await new Promise((resolve) => setTimeout(resolve, 750));
  const lateEvidence = await waitForEvidence(client, requestId, 'probe-completed');
  ensure(
    !lateEvidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('timeout-await-unexpected-resumed')),
    'ATD-E1 timeout call resumed after timeout.'
  );
  console.log('scenario ATD-E1 passed');
}

async function waitForEvidence(
  client: ZlinkStreamConnector,
  requestId: string,
  marker: string
): Promise<AwaitEvidenceRes> {
  return await client
    .request({ requestId, marker, timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
