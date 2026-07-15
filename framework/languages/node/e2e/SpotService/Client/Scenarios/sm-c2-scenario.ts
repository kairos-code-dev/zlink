// SM-C2: spot → channel messaging 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotOutboundRouteRes,
  SpotOutboundRouteReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmC2(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-c2-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(
    created.spotRid === spotRid && created.nodeRid === 'play-a',
    'SM-C2 spot was not created on play-a.'
  );

  const outbound = await postJson<SpotOutboundRouteRes>(options.playAUrl, '/spot/outbound', {
    spotRid,
    marker: 'sm-c2'
  } satisfies SpotOutboundRouteReq);
  ensure(outbound.accepted, 'SM-C2 outbound route was not accepted.');

  const negative = await postJson<SpotOutboundRouteRes>(options.playAUrl, '/spot/outbound-negative', {
    spotRid,
    marker: 'sm-c2-missing'
  } satisfies SpotOutboundRouteReq);
  ensure(negative.accepted, 'SM-C2 negative outbound route was not accepted.');

  const expectedEvidence = [
    `spot-outbound|rid=play-a|spot=${spotRid}|echo=echo-sm-c2|notify=notify-sm-c2`,
    `spot-msg|rid=play-a|spot=${spotRid}|marker=sm-c2-publish`,
    `spot-outbound-negative|rid=play-a|spot=${spotRid}|requestFailed=True`,
    'channel-echo|value=sm-c2',
    'channel-notify|marker=notify-sm-c2',
    'dispatch-error|surface=channel|kind=request|reason=handlerMissing|action=replyError|packet=MissingChannelReq',
    'dispatch-error|surface=channel|kind=send|reason=handlerMissing|action=drop|packet=MissingChannelNotify'
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-C2 evidence mismatch.'
  );

  console.log('scenario SM-C2 passed');
}
