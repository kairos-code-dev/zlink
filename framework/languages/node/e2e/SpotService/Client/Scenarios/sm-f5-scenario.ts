// SM-F5: Spot lifecycle과 MeshNode lifecycle 분리 시나리오를 검증한다.
import type {
  ChannelRouteRes,
  ChannelRouteReq,
  CloseSpotRes,
  CloseSpotReq,
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotMixedRouteRes,
  SpotMixedRouteReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF5(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-f5-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-F5 did not create the requested spot.');
  ensure(created.nodeRid === 'play-a', 'SM-F5 created spot on the wrong node.');

  const mixed = await postJson<SpotMixedRouteRes>(options.playBUrl, '/spot/mixed-route/request', {
    spotRid,
    targetNodeRid: 'play-a',
    channelValue: 'sm-f5-before-close',
    delta: 13
  } satisfies SpotMixedRouteReq);
  ensure(mixed.channelReply === 'echo-sm-f5-before-close', 'SM-F5 pre-close channel reply mismatch.');
  ensure(mixed.spotValue === 13, 'SM-F5 pre-close spot route reply mismatch.');

  const closed = await postJson<CloseSpotRes>(options.playAUrl, '/spot/close', {
    spotRid
  } satisfies CloseSpotReq);
  ensure(closed.closed, 'SM-F5 did not close the spot.');

  const channelAfterClose = await postJson<ChannelRouteRes>(options.playBUrl, '/channel/route/request', {
    targetNodeRid: 'play-a',
    value: 'sm-f5-after-close'
  } satisfies ChannelRouteReq);
  ensure(channelAfterClose.value === 'echo-sm-f5-after-close', 'SM-F5 channel reply after spot close mismatch.');

  const expectedEvidence = [
    'channel-echo|value=sm-f5-before-close',
    `spot-state-request|rid=play-a|spot=${spotRid}|value=13`,
    `spot-closing|rid=play-a|spot=${spotRid}`,
    'channel-echo|value=sm-f5-after-close'
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-F5 channel independence evidence mismatch.'
  );

  console.log('scenario SM-F5 passed');
}
