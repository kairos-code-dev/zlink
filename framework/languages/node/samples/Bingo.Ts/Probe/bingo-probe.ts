type ReadyEvent = {
  endpoint?: string;
  streamEndpoint?: string;
  httpEndpoint?: string;
};

function assertBingoTopologyReady(ready: ReadyEvent[]): void {
  if (ready.length !== 4) {
    throw new Error(`Expected 4 Bingo server ready events, got ${ready.length}.`);
  }
  for (const event of ready) {
    if (event.endpoint === undefined) {
      throw new Error(`Bingo ready event is missing endpoint: ${JSON.stringify(event)}`);
    }
  }
}

export { assertBingoTopologyReady };
