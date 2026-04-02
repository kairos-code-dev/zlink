package main

import (
	"fmt"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.MustStep("NewContext", err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	registry, err := ctx.Registry()
	samplecommon.MustStep("Registry", err)
	defer func() { samplecommon.MustStep("registry.Close", registry.Close()) }()

	discovery, err := ctx.Discovery(zlink.ServiceTypeSocket, "service-found")
	samplecommon.MustStep("Discovery", err)
	defer func() { samplecommon.MustStep("discovery.Close", discovery.Close()) }()

	pub, err := ctx.PubSocket()
	samplecommon.MustStep("PubSocket", err)

	registryPub := samplecommon.UniqueTCP("registry-pub")
	registryRouter := samplecommon.UniqueTCP("registry-router")
	serviceEndpoint := samplecommon.UniqueTCP("discovery-service")

	samplecommon.MustStep("registry.Bind", registry.Bind(registryPub, registryRouter))
	samplecommon.MustStep("discovery.ConnectRegistry", discovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("pub.AttachDiscovery", pub.AttachDiscovery(discovery))
	samplecommon.MustStep("pub.Bind", pub.Bind(serviceEndpoint))

	_ = serviceEndpoint
	_ = samplecommon.WaitTopologyEntry(registry.TopologySnapshot, "service-found")

	fmt.Println("[discovery-registry] registry: bind -> discovery: connect -> discover: \"service-found\"")
}
