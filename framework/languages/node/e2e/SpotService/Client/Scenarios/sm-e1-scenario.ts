// SM-E1: spot route 미등록 request 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotMissingMsgRes,
  SpotMissingMsgReq,
  SpotMissingHandlerRes,
  SpotMissingHandlerReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmE1(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-e1-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid && created.nodeRid === 'play-a', 'SM-E1 spot was not created on play-a.');

  const missingRequest = await postJson<SpotMissingHandlerRes>(options.playAUrl, '/spot/missing-handler/request', {
    spotRid
  } satisfies SpotMissingHandlerReq);
  ensure(missingRequest.failed, 'SM-E1 missing handler request did not fail.');

  const missingCommand = await postJson<SpotMissingMsgRes>(options.playAUrl, '/spot/missing-handler/command', {
    spotRid,
    marker: 'missing-command'
  } satisfies SpotMissingMsgReq);
  ensure(missingCommand.sent, 'SM-E1 missing handler command was not sent.');

  const expectedEvidence = [
    'dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=failCaller|packet=MissingSpotReq',
    'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=MissingSpotMsg'
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-E1 missing handler evidence mismatch.'
  );

  console.log('scenario SM-E1 passed');
}
