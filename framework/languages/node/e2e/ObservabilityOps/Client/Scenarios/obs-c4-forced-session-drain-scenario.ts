// OBS-C4: Shutdown closing callback + 세션 종료 통지 시나리오를 검증한다.
import {
  ZlinkStreamDispatchMode,
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec
} from '@zlink-systems/stream-connector';
import { options, require, session } from '../Support/scenario-support.js';
import { metric, metrics, startDrain, waitFor, waitForDrain } from '../Support/observability-support.js';

export async function runObsC4(): Promise<void> {
  const connector = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: true, intervalMs: 100, timeoutMs: 5000 },
    reconnect: { enabled: false }
  });
  await connector.connect();
  await waitFor(async () => await metrics(session),
    (values) => values.some((value) => value.name === 'zlink.stream.connections.active' && value.value === 1),
    'OBS-C4 server did not observe the active STREAM session');
  await startDrain(session, 100);
  const status = await waitForDrain(session, (value) => value.result?.kind === 'force-stopped',
    'OBS-C4 Session did not force stop', 5000);
  require(status.result?.reason === 'DeadlineExceeded', `OBS-C4 force reason was '${status.result?.reason}'.`);
  await waitFor(async () => connector.closeReason, (value) => value === 'ServerDrain',
    'OBS-C4 connector did not preserve server drain close reason');
  require(metric(await metrics(session), 'zlink.drain.forced').value >= 1,
    'OBS-C4 forced drain metric was not incremented.');
  await connector.close();
}
