// OBS-B3: fanout·lease 계기와 카디널리티 시나리오를 검증한다.
import { post, require, unique, workflowA, workflowB } from '../Support/scenario-support.js';
import { metric, metrics, waitFor } from '../Support/observability-support.js';
import type { WorkflowApplyRes } from '../../Shared/messages.js';

export async function runObsB3(): Promise<void> {
  const orderId = unique('obs-b3-order');
  await post<WorkflowApplyRes>(workflowA, '/workflows', { orderId, value: 1 });
  const published = await waitFor(async () => await metrics(workflowA),
    (values) => values.some((value) => value.name === 'zlink.fanout.published' && value.value >= 1),
    'OBS-B3 fanout publish metric missing');
  metric(published, 'zlink.fanout.published');
  const received = await waitFor(async () => await metrics(workflowB),
    (values) => values.some((value) => value.name === 'zlink.fanout.received' && value.value >= 1),
    'OBS-B3 fanout receive metric missing');
  metric(received, 'zlink.fanout.received');
  const lease = await waitFor(async () => [...await metrics(workflowA), ...await metrics(workflowB)],
    (values) => values.some((value) => value.name === 'zlink.location.owner_lease.renew.lateness'),
    'OBS-B3 owner lease lateness metric missing');
  const all = [...published, ...received, ...lease];
  const forbidden = ['correlation_id', 'flow_id', 'actor_id', 'spot_rid'];
  require(all.every((value) => forbidden.every((label) => !(label in value.tags))),
    'OBS-B3 emitted a high-cardinality metric label.');
  require(!all.some((value) => value.name === 'zlink.fanout.dropped' && value.value === 0),
    'OBS-B3 emitted an unsupported zero fanout.dropped instrument.');
  metric(all, 'zlink.location.owner_lease.renew.lateness');
}
