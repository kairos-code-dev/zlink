import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { EvidenceWaitReq, WorkflowRes, WorkflowReq } from '../../../Shared/messages';
import { PacketNames } from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createWorkflowEndpoints(
  evidence: EvidenceStore,
  channel: ZLinkChannelClient,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'workflow', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/workflow/request', handle: (body) => requestWorkflowWithRetry(channel, body as WorkflowReq) },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((line) => line.includes(request.contains), timeout);
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function requestWorkflowWithRetry(
  channel: ZLinkChannelClient,
  request: WorkflowReq
): Promise<WorkflowRes> {
  const deadline = Date.now() + 30000;
  let last: unknown;
  while (Date.now() < deadline) {
    try {
      return await channel
        .requestToChannel('workflow', request)
        .packetName(PacketNames.workflowReq)
        .timeout(5000)
        .submit<WorkflowRes>();
    } catch (error) {
      last = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(`Timed out waiting for workflow request channel route: ${last instanceof Error ? last.message : String(last)}`);
}
