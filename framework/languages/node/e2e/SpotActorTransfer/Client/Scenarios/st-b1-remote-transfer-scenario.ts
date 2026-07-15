// ST-B1: remote transfer 성공 순서와 state 복원 시나리오를 검증한다.
import { SpotActorTransferNames, runRemoteTransfer, unique } from '../Support/scenario-support';

export async function runStB1(): Promise<void> {
  await runRemoteTransfer('ST-B1', unique('actor-remote-ok'), SpotActorTransferNames.actorTypeStateful, 21, true);
}
