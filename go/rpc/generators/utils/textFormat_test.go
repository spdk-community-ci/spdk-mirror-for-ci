/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Intel Corporation.
 *   All rights reserved.
 */

package utils

import (
	"testing"
)

func Test_snakeCaseToCamelCase(t *testing.T) {
	type args struct {
		s string
	}
	tests := []struct {
		name string
		args args
		want string
	}{
		{
			name: "empty string",
			args: args{s: ""},
			want: "",
		},
		{
			name: "start / end with underscore",
			args: args{s: "_test_snake_case_"},
			want: "TestSnakeCase",
		},
		{
			name: "capital letters",
			args: args{s: "Test_Snake_case"},
			want: "TestSnakeCase",
		},
		{
			name: "screaming snake case",
			args: args{s: "TEST_SNAKE_CASE"},
			want: "TestSnakeCase",
		},
		{
			name: "normal snake case",
			args: args{s: "test_snake_case"},
			want: "TestSnakeCase",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := SnakeCaseToCamelCase(tt.args.s); got != tt.want {
				t.Errorf("snakeCaseToCamelCase() = %v, want %v", got, tt.want)
			}
		})
	}
}

func Test_camelCaseToSnakeCase(t *testing.T) {
	type args struct {
		s string
	}
	tests := []struct {
		name string
		args args
		want string
	}{
		{
			name: "empty string",
			args: args{s: ""},
			want: "",
		},
		{
			name: "regular camel case",
			args: args{s: "TestSnakeCase"},
			want: "test_snake_case",
		},
		{
			name: "camel case with small first letter",
			args: args{s: "testSnakeCase"},
			want: "test_snake_case",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := CamelCaseToSnakeCase(tt.args.s); got != tt.want {
				t.Errorf("CamelCaseToSnakeCase() = %v, want %v", got, tt.want)
			}
		})
	}
}
