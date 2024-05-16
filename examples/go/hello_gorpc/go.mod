module github.com/spdk/spdk/examples/go-rpc

go 1.19

require github.com/spdk/spdk/go/rpc v0.0.0

require golang.org/x/text v0.14.0 // indirect

replace github.com/spdk/spdk/go/rpc v0.0.0 => ./../../../go/rpc
