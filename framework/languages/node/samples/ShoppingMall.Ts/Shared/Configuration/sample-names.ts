const SampleNames = {
  apiA: 'api-a',
  apiB: 'api-b',
  workflowA: 'workflow-a',
  workflowB: 'workflow-b',
  orderWorkflowChannelPrefix: 'shoppingmall.order-workflow',
  orderWorkflowSpotMesh: 'shoppingmall.order-workflow.spot',
  clientTimeout: 15000,
  requestTimeout: 5000,
  pollDelay: 50
} as const;

function orderWorkflowChannelFor(instanceId: string): string {
  return `${SampleNames.orderWorkflowChannelPrefix}.${instanceId}`;
}

export { SampleNames, orderWorkflowChannelFor };
