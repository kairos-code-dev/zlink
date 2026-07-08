import type { OrderState, ServerAssertionReq, StartOrderReq } from '../../../../Shared/Contracts/messages';

interface CommerceOrderStorePort {
  createPendingMapping(idempotencyKey: string, orderId: string, ownerInstanceId: string): { created: boolean };
  getOrder(orderId: string): { state: OrderState };
  deleteProjection(orderId: string): { deleted: boolean };
  assertEvidence(request: ServerAssertionReq): { passed: boolean; evidence: string[] };
}

interface OrderWorkflowStorePort {
  startOrder(request: StartOrderReq, instanceId: string): { orderId: string; status: string };
  createInventoryReserved(request: StartOrderReq, instanceId: string): { orderId: string; status: string };
  continueOrder(orderId: string, instanceId: string): { state: OrderState };
  rebuildProjection(orderId: string): { state: OrderState };
}

export type { CommerceOrderStorePort, OrderWorkflowStorePort };
