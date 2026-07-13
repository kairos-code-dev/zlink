abstract class OrderWorkflowRouterPort {
  abstract start(request: import('../../../Shared/Contracts/messages').StartOrderReq): Promise<import('../../../Shared/Contracts/messages').StartOrderRes>;
  abstract prepareInventory(request: import('../../../Shared/Contracts/messages').StartOrderReq): Promise<import('../../../Shared/Contracts/messages').StartOrderRes>;
  abstract continue(orderId: string): Promise<import('../../../Shared/Contracts/messages').ContinueOrderWorkflowRes>;
  abstract rebuild(orderId: string): Promise<import('../../../Shared/Contracts/messages').RebuildOrderProjectionRes>;
}

export { OrderWorkflowRouterPort };
