/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Intel Corporation.
 *   All rights reserved.
 */

package main

import (
	"bytes"
	"embed"
	"encoding/json"
	"fmt"
	"go/format"
	"html/template"
	"log"
	"os"

	"github.com/spdk/spdk/go/rpc/generators/utils"
)

var (
	//go:embed structs.tmpl
	rpcTmpl      embed.FS
	templateName = "structs.tmpl"
)

func main() {
	// Verify whether user provided a path to RPC calls schema file
	if len(os.Args) < 2 {
		log.Fatal("please provide path to RPC calls schema file")
	}

	rpcCalls, err := getRpcCallsFromSchema(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}

	formatRpcCalls(rpcCalls)

	tmpl, err := template.New(templateName).Funcs(template.FuncMap{
		"snakeCaseToCamelCase": utils.SnakeCaseToCamelCase,
	}).ParseFS(rpcTmpl, templateName)
	if err != nil {
		log.Fatal(err)
	}

	var executedTemplate bytes.Buffer
	err = tmpl.ExecuteTemplate(&executedTemplate, templateName, rpcCalls.RpcCalls)
	if err != nil {
		log.Fatal(err)
	}

	formattedTemplate, err := format.Source(executedTemplate.Bytes())
	if err != nil {
		log.Fatal(err)
	}

	// Print result to stdout
	fmt.Print(string(formattedTemplate))
}

func getRpcCallsFromSchema(path string) (*RpcCallCollection, error) {
	file, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("error when reading a file at path: %s, err: %v",
			path, err)
	}

	rpcCalls := &RpcCallCollection{}

	err = json.Unmarshal(file, &rpcCalls)
	if err != nil {
		return nil, fmt.Errorf("error when unmarshalling rpc calls, err: %v", err)
	}
	return rpcCalls, err
}

func formatRpcCalls(rpcCalls *RpcCallCollection) {
	for _, call := range rpcCalls.RpcCalls {
		for _, parameter := range call.Parameters {
			switch parameter.Type {
			case "number", "int":
				parameter.Type = "int64"
			case "boolean":
				parameter.Type = "bool"
			case "string map":
				parameter.Type = "string"
			default:
			}
		}
	}
}

type RpcCall struct {
	Name        string       `json:"method"`
	Description string       `json:"description"`
	Parameters  []*Parameter `json:"params"`
}

type Parameter struct {
	Param       string `json:"param"`
	Type        string `json:"type"`
	Description string `json:"description"`
	IsRequired  bool   `json:"required"`
}

type RpcCallCollection struct {
	RpcCalls []*RpcCall `json:"methods"`
}
