abstract class OrderWorkflowRouterPort {
  abstract requestWorkflow<TResponse>(payload: unknown, packetName: string, ownerKey: string): Promise<TResponse>;
}

export { OrderWorkflowRouterPort };
