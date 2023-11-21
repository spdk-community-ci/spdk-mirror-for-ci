/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Intel Corporation.
 *   All rights reserved.
 */

package utils

import (
	"strings"
	"unicode"

	"golang.org/x/text/cases"
	"golang.org/x/text/language"
)

func CamelCaseToSnakeCase(s string) string {
	if len(s) == 0 {
		return ""
	}
	var result []string

	for i, char := range s {
		if i > 0 && unicode.IsUpper(char) {
			result = append(result, "_")
		}
		result = append(result, strings.ToLower(string(char)))
	}
	return strings.Join(result, "")
}

func SnakeCaseToCamelCase(s string) string {
	afterSplit := strings.SplitAfter(strings.ToLower(s), "_")
	var res []string
	for _, str := range afterSplit {
		res = append(res, cases.Title(language.Und).String(str))
	}

	return strings.Replace(strings.Join(res, ""), "_", "", -1)
}
