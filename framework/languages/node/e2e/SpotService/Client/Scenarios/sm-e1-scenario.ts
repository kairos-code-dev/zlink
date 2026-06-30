import type {
  CreateSpotReply,
  CreateSpotReq,
  EvidenceWaitRequest,
  SpotMissingCommandReply,
  SpotMissingCommandReq,
  SpotMissingHandlerReply,
  SpotMissingHandlerReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmE1(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-e1-${Date.now()}`;
  const created = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid && created.nodeRid === 'play-a', 'SM-E1 spot was not created on play-a.');

  const missingRequest = await postJson<SpotMissingHandlerReply>(options.playAUrl, '/spot/missing-handler/request', {
    spotRid
  } satisfies SpotMissingHandlerReq);
  ensure(missingRequest.failed, 'SM-E1 missing handler request did not fail.');

  const missingCommand = await postJson<SpotMissingCommandReply>(options.playAUrl, '/spot/missing-handler/command', {
    spotRid,
    marker: 'missing-command'
  } satisfies SpotMissingCommandReq);
  ensure(missingCommand.sent, 'SM-E1 missing handler command was not sent.');

  const expectedEvidence = [
    'dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=replyError|packet=MissingSpotReq',
    'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=MissingSpotCommand'
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-E1 missing handler evidence mismatch.'
  );

  console.log('scenario SM-E1 passed');
}
