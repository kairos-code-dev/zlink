package main

import (
	"fmt"
	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
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
	serviceEndpoint := samplecommon.UniqueTCP("registry-query-service")

	samplecommon.MustStep("registry.Bind", registry.Bind(registryPub, registryRouter))
	samplecommon.MustStep("discovery.ConnectRegistry", discovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("pub.AttachDiscovery", pub.AttachDiscovery(discovery))
	samplecommon.MustStep("pub.Bind", pub.Bind(serviceEndpoint))

	_ = serviceEndpoint
	_ = samplecommon.WaitTopologyEntry(registry.TopologySnapshot, "sample")

	query, err := ctx.RegistryQueryClient()
	samplecommon.MustStep("RegistryQueryClient", err)
	defer func() { samplecommon.MustStep("query.Close", query.Close()) }()
	samplecommon.MustStep("query.Connect", query.Connect(registryRouter))

	_ = samplecommon.WaitTopologyEntry(func() ([]zlink.RegistryTopologyEntry, error) {
		return query.Snapshot(nil)
	}, "sample")

	fmt.Println("[registry-query] service: \"sample\" -> snapshot: found")
}
