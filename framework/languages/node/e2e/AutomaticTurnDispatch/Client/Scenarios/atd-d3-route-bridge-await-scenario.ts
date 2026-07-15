// ATD-D3: route bridge를 경유한 target Spot await 순서 검증한다.
import type {
  EnsureSpotRes,
  EnsureSpotReq,
  ProbeMsg,
  AwaitMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export async function runYdD3(client: ZlinkStreamConnector): Promise<void> {
  const requestId = `ATD-D3-${uniqueId()}`;
  const spotRid = `await-route-bridge-${uniqueId()}`;
  await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<EnsureSpotRes>();

  await client
    .send({ requestId, delayMs: 250, correlationId: 'route-bridge' } satisfies AwaitMsg)
    .packetName('AwaitMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await client
    .send({ requestId, marker: 'route-bridge-probe' } satisfies ProbeMsg)
    .packetName('ProbeMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .submit();

  const evidence = await client
    .request({ requestId, marker: 'await-completed' } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-b')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'await-started',
    'await-released',
    'probe-started',
    'probe-completed',
    'await-resumed',
    'await-completed'
  ], 'ATD-D3 route bridge marker order mismatch.');
  ensure(evidence.evidence.some((line) =>
    line.includes('await-started|rid=play-b') && line.includes('handler=spot')),
  'ATD-D3 target Spot handler marker missing.');
  console.log('scenario ATD-D3 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
