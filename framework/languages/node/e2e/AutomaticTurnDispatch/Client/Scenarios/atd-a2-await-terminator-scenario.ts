// ATD-A2: await terminator가 Spot turn을 반납하고 continuation을 재개하는 순서 검증한다.
import type {
  ProbeMsg,
  AwaitMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA2(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `ATD-A2-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350, correlationId: 'corr-a2' } satisfies AwaitMsg)
    .packetName('AwaitMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await client
    .request({ requestId, marker: 'await-released', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit();
  await client
    .send({ requestId, marker: 'await-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'probe-completed', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'await-started',
    'await-released',
    'await-resumed',
    'await-completed',
    'probe-started',
    'probe-completed'
  ], 'ATD-A2 marker order mismatch.');
  console.log('scenario ATD-A2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
