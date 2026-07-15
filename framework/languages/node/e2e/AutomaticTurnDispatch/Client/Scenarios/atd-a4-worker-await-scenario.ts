// ATD-A4: I/O worker yield 중 같은 Spot의 다른 callback 진행 검증한다.
import type {
  ProbeMsg,
  WorkerAwaitMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdA4(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `ATD-A4-${uniqueId()}`;
  await client
    .send({ requestId, delayMs: 350 } satisfies WorkerAwaitMsg)
    .packetName('WorkerAwaitMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await client
    .request({ requestId, marker: 'worker-await-released', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit();
  await client
    .send({ requestId, marker: 'worker-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'worker-await-completed', timeoutMilliseconds: 30000 } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'worker-await-started',
    'worker-await-released',
    'probe-started',
    'probe-completed',
    'worker-await-resumed',
    'worker-await-completed'
  ], 'ATD-A4 marker order mismatch.');
  console.log('scenario ATD-A4 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
