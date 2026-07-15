// ATD-E2: await cancellation 뒤 같은 Spot의 후속 처리 복구 검증한다.
import type {
  EnsureSpotRes,
  EnsureSpotReq,
  ProbeMsg,
  AwaitCancelMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdE2(client: ZlinkStreamConnector): Promise<void> {
  const spotRid = `await-cancel-${uniqueId()}`;
  const spot = await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotRes>();
  ensure(spot.spotRid === spotRid, 'ATD-E2 spot creation mismatch.');

  const requestId = `ATD-E2-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 800, cancelAfterMs: 100 } satisfies AwaitCancelMsg)
    .packetName('AwaitCancelMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'cancel-await-completed');

  await client
    .send({ requestId, marker: 'cancel-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  const evidence = await waitForEvidence(client, requestId, 'probe-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'cancel-await-started',
    'cancel-await-released',
    'cancel-await-completed',
    'probe-started',
    'probe-completed'
  ], 'ATD-E2 cancellation cleanup marker order mismatch.');
  ensure(
    evidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('marker=cancel-probe')),
    'ATD-E2 post-cancel probe marker missing.'
  );

  await new Promise((resolve) => setTimeout(resolve, 850));
  const lateEvidence = await waitForEvidence(client, requestId, 'probe-completed');
  ensure(
    !lateEvidence.evidence.some((line) => line.includes(`request=${requestId}`) && line.includes('cancel-await-unexpected-resumed')),
    'ATD-E2 canceled call resumed after cancellation.'
  );
  console.log('scenario ATD-E2 passed');
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
