import type {
  SpotMissingTargetCommandReply,
  SpotMissingTargetCommandReq,
  SpotMissingTargetReply,
  SpotMissingTargetReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF4(options: ClientOptions): Promise<void> {
  const missingTarget = await postJson<SpotMissingTargetReply>(options.playAUrl, '/spot/missing-target/request', {
    spotRid: `missing-spot-sm-f4-${Date.now()}`
  } satisfies SpotMissingTargetReq);
  ensure(missingTarget.failed, 'SM-F4 missing target request did not fail.');
  ensure(
    missingTarget.evidence.some((line) =>
      line.includes('dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=replyError|packet=StateReq')),
    'SM-F4 missing target request error evidence mismatch.'
  );

  const missingCommand = await postJson<SpotMissingTargetCommandReply>(options.playAUrl, '/spot/missing-target/command', {
    spotRid: `missing-spot-sm-f4-command-${Date.now()}`,
    marker: 'missing-target-command'
  } satisfies SpotMissingTargetCommandReq);
  ensure(missingCommand.sent, 'SM-F4 missing target command was not sent.');
  ensure(
    missingCommand.evidence.some((line) =>
      line.includes('dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=StateCommand')),
    'SM-F4 missing target command drop evidence mismatch.'
  );

  console.log('scenario SM-F4 passed');
}
