const SampleNames = {
  apiChannel: 'supportchat.api',
  supportChannel: 'supportchat.support',
  sessionStreamNode: 'supportchat-session-stream',
  conversationSpotMesh: 'supportchat-conversations',
  conversationIdMetadataKey: 'conversation-id'
} as const;

const SampleTimings = {
  requestTimeout: 2000,
  idleTimeout: 300,
  closeGraceTimeout: 200,
  clientTimeout: 10000
} as const;

export {
  SampleNames,
  SampleTimings
};
