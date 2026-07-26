// SM-F4: target Spot 없음과 stale generation 시나리오를 검증한다.
import type {
  SpotMissingTargetRes,
  SpotMissingTargetReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF4(options: ClientOptions): Promise<void> {
  const missingTarget = await postJson<SpotMissingTargetRes>(options.playAUrl, '/spot/missing-target/request', {
    spotRid: `missing-spot-sm-f4-${Date.now()}`
  } satisfies SpotMissingTargetReq);
  ensure(missingTarget.failed, 'SM-F4 missing target request did not fail.');

  console.log('scenario SM-F4 passed');
}
