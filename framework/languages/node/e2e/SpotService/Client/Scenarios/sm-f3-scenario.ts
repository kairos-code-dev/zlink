// SM-F3: ChannelName·RID direct·Spot direct namespace 분리 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotMixedRouteRes,
  SpotMixedRouteReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF3(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-f3-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-F3 did not create the requested spot.');
  ensure(created.nodeRid === 'play-a', 'SM-F3 created spot on the wrong node.');

  const mixed = await postJson<SpotMixedRouteRes>(options.playBUrl, '/spot/mixed-route/request', {
    spotRid,
    targetNodeRid: 'play-a',
    channelValue: 'sm-f3-channel',
    delta: 11
  } satisfies SpotMixedRouteReq);
  ensure(mixed.spotRid === spotRid, 'SM-F3 mixed route reached the wrong spot.');
  ensure(mixed.channelReply === 'echo-sm-f3-channel', 'SM-F3 channel reply mismatch.');
  ensure(mixed.spotValue === 11, 'SM-F3 spot route reply mismatch.');
  const expectedEvidence = [
    'channel-echo|value=sm-f3-channel',
    `spot-state-request|rid=play-a|spot=${spotRid}|value=11`
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-F3 mixed route evidence mismatch.'
  );

  console.log('scenario SM-F3 passed');
}
