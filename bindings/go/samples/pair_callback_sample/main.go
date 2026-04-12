package main

import (
	"bytes"
	"fmt"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	server, err := ctx.PairSocket()
	samplecommon.Must(err)
	defer server.Close()
	client, err := ctx.PairSocket()
	samplecommon.Must(err)
	defer client.Close()

	serverMon := samplecommon.OpenMonitor(server)
	defer serverMon.Close()
	clientMon := samplecommon.OpenMonitor(client)
	defer clientMon.Close()

	endpoint := samplecommon.UniqueTCP("pair-callback")
	samplecommon.Must(server.Bind(endpoint))
	samplecommon.Must(client.Connect(endpoint))
	samplecommon.WaitConnected(serverMon, clientMon)

	type result struct {
		payload string
		err     error
	}
	delivered := make(chan result, 1)
	samplecommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		part, err := received.SinglePartOrError()
		if err != nil {
			delivered <- result{err: err}
			return
		}
		delivered <- result{payload: string(part.Data())}
	}))

	sent := "hello-pair"
	samplecommon.Must(client.Send(zlink.SendFlagsNone, samplecommon.Message(sent)))
	out := <-delivered
	samplecommon.Must(out.err)
	got := out.payload
	if !bytes.Equal([]byte(got), []byte(sent)) {
		samplecommon.Must(fmt.Errorf("unexpected payload %q", got))
	}

	fmt.Printf("[pair/callback] send: %q -> recv: %q\n", sent, got)
}
