import type {
  CreateSpotReply,
  CreateSpotReq,
  SpotPublishObserveReply,
  SpotPublishReply,
  SpotPublishReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmC4(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-c4-${Date.now()}`;
  const created = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(
    created.spotRid === spotRid && created.nodeRid === 'play-a',
    'SM-C4 publish spot was not created on play-a.'
  );

  const unsubscribedSpotRid = `spot-sm-c4-unsubscribed-${Date.now()}`;
  const unsubscribed = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create-alternate', {
    spotRid: unsubscribedSpotRid
  } satisfies CreateSpotReq);
  ensure(
    unsubscribed.spotRid === unsubscribedSpotRid && unsubscribed.nodeRid === 'play-a',
    'SM-C4 unsubscribed spot was not created on play-a.'
  );

  const marker = 'sm-c4-publish';
  const waitTask = postJson<SpotPublishObserveReply>(options.playAUrl, '/spot/publish/wait', {
    spotRid,
    marker
  } satisfies SpotPublishReq);
  const publish = await postJson<SpotPublishReply>(options.gatewayUrl, '/spot/publish', {
    spotRid,
    marker
  } satisfies SpotPublishReq);
  const observe = await waitTask;

  ensure(publish.operation === 'spot.sm-c4-publish', 'SM-C4 publish operation mismatch.');
  ensure(publish.publisherRid === 'gateway', 'SM-C4 publisher was not the publish-only gateway.');
  ensure(publish.spotRid === spotRid, 'SM-C4 publish target spot mismatch.');
  ensure(observe.operation === 'spot.sm-c4-observe', 'SM-C4 observe operation mismatch.');
  ensure(observe.received, 'SM-C4 publish-only gateway event was not received.');
  ensure(
    publish.evidence.some((line) => line.includes(`spot-publish|rid=gateway|spot=${spotRid}|marker=${marker}`)),
    'SM-C4 gateway evidence did not include publish marker.'
  );
  ensure(
    observe.evidence.some((line) => line.includes(`spot-event|rid=play-a|spot=${spotRid}|marker=${marker}`)),
    'SM-C4 evidence did not include spot event publish.'
  );
  ensure(
    observe.evidence.every((line) => !line.includes(`spot-event|rid=play-a|spot=${unsubscribedSpotRid}|marker=${marker}`)),
    'SM-C4 unsubscribed spot received publish event.'
  );

  console.log('scenario SM-C4 passed');
}
