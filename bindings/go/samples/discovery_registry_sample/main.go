package main

import (
	"fmt"
	zlink "zlink.systems/zlink"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
	// --8<-- [start:doc]
	ctx, err := zlink.NewContext()
	samplecommon.MustStep("NewContext", err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	registry, err := ctx.Registry()
	samplecommon.MustStep("Registry", err)
	defer func() { samplecommon.MustStep("registry.Close", registry.Close()) }()

	discovery, err := ctx.Discovery(zlink.AutoConnectFanout, "sample")
	samplecommon.MustStep("Discovery", err)

	pub, err := ctx.PubSocket()
	samplecommon.MustStep("PubSocket", err)
	defer func() { samplecommon.MustStep("discovery.Close", discovery.Close()) }()

	registryPub := samplecommon.UniqueTCP("registry-pub")
	registryRouter := samplecommon.UniqueTCP("registry-router")
	serviceEndpoint := samplecommon.UniqueTCP("discovery-service")

	samplecommon.MustStep("registry.Bind", registry.Bind(registryPub, registryRouter))
	samplecommon.MustStep("discovery.ConnectRegistry", discovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("pub.AttachDiscovery", pub.AttachDiscovery(discovery))
	samplecommon.MustStep("pub.Bind", pub.Bind(serviceEndpoint))

	_ = serviceEndpoint
	_ = samplecommon.WaitTopologyEntry(registry.Topology, "sample")

	fmt.Println("[discovery-registry] service: \"sample\" -> discovered")
	// --8<-- [end:doc]
}
