// SM-C3: spot → spot messaging 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotToSpotNegativeRes,
  SpotToSpotNegativeRouteReq,
  SpotToSpotRes,
  SpotToSpotRouteReq,
  SpotToSpotTimeoutRes,
  SpotToSpotTimeoutRouteReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmC3(options: ClientOptions): Promise<void> {
  const sourceSpotRid = `spot-sm-c3-source-${Date.now()}`;
  const targetSpotRid = `spot-sm-c3-target-${Date.now()}`;
  for (const spotRid of [sourceSpotRid, targetSpotRid]) {
    const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
      spotRid
    } satisfies CreateSpotReq);
    ensure(
      created.spotRid === spotRid && created.nodeRid === 'play-a',
      'SM-C3 spot was not created on play-a.'
    );
  }

  const failures: string[] = [];
  try {
    const direct = await postJson<SpotToSpotRes>(options.playAUrl, '/spot/to-spot/request', {
      sourceSpotRid,
      targetSpotRid,
      marker: 'direct'
    } satisfies SpotToSpotRouteReq);
    ensure(direct.sourceSpotRid === sourceSpotRid, 'SM-C3 source spot mismatch.');
    ensure(direct.targetSpotRid === targetSpotRid, 'SM-C3 target spot mismatch.');
    ensure(direct.targetValue >= 3, 'SM-C3 target state was not updated.');
  } catch (error) {
    failures.push(`request=${formatError(error)}`);
  }

  try {
    const timeout = await postJson<SpotToSpotTimeoutRes>(options.playAUrl, '/spot/to-spot/timeout', {
      sourceSpotRid,
      targetSpotRid,
      marker: 'slow'
    } satisfies SpotToSpotTimeoutRouteReq);
    ensure(timeout.failed, 'SM-C3 slow target request did not time out.');
  } catch (error) {
    failures.push(`timeout=${formatError(error)}`);
  }

  try {
    const negative = await postJson<SpotToSpotNegativeRes>(options.playAUrl, '/spot/to-spot/negative', {
      sourceSpotRid,
      targetSpotRid,
      marker: 'missing'
    } satisfies SpotToSpotNegativeRouteReq);
    ensure(negative.requestFailed, 'SM-C3 missing target handler request did not fail.');
  } catch (error) {
    failures.push(`negative=${formatError(error)}`);
  }

  if (failures.length === 0) {
    const expectedEvidence = [
      `spot-to-spot|rid=play-a|source=${sourceSpotRid}|target=${targetSpotRid}|value=`,
      `spot-state-command|rid=play-a|spot=${targetSpotRid}|marker=sm-c3-send-direct`,
      `spot-msg|rid=play-a|spot=${targetSpotRid}|marker=sm-c3-publish-direct`,
      `spot-to-spot-timeout|rid=play-a|source=${sourceSpotRid}|target=${targetSpotRid}|failed=True`,
      `spot-to-spot-negative|rid=play-a|source=${sourceSpotRid}|target=${targetSpotRid}|requestFailed=True`,
      'dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=replyError|packet=MissingSpotReq',
      'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=MissingSpotMsg'
    ];
    const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: expectedEvidence,
      timeoutMilliseconds: 30000
    } satisfies EvidenceWaitReq);
    ensure(
      expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
      'SM-C3 evidence mismatch.'
    );
    console.log('scenario SM-C3 passed');
    return;
  }

  throw new Error(`SM-C3 spot-to-spot messaging incomplete: ${failures.join('; ')}`);
}

function formatError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
