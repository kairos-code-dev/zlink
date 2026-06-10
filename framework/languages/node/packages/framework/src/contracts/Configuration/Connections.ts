export interface ZLinkEndpointConnections {
  connect(endpoint: string): this;
  bind(endpoint: string): this;
}
