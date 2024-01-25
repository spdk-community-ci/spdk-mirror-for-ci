#!/usr/bin/env python3
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation.
#  All rights reserved.

import argparse
from datetime import date
from json import load
from os import path
from sys import stdout
from textwrap import wrap

license = f"""#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) {date.today().year} Intel Corporation.
#  All rights reserved.

#  Auto-generated code. DO NOT EDIT
"""

rpc_tmpl = """
def $rpc_method($pparams):
    \"\"\"$rpc_description$rpc_params_description$rpc_response\"\"\"
    params = dict()$all_params
    return client.call('$rpc_method', params)
"""

LINE_MAX_LENGTH = 140


def print_param(params, signature_len):
    param_list = ["client"]
    param_list.extend([param["param"] for param in params if param["required"]])
    param_list.extend(
        [f"{param['param']}=None" for param in params if not param["required"]]
    )

    line = []
    pretty_params = []
    for param in param_list:
        line.append(param)
        if signature_len + len(", ".join(line)) + len(', ') > LINE_MAX_LENGTH:
            line.pop()
            pretty_params.append(', '.join(line) + ',')
            pretty_params.append('\n'+' '*signature_len)
            line = [param]
    pretty_params.append(', '.join(line))

    return pretty_params


def generate_rpcs(schema):
    content = license

    for method in schema["methods"]:
        signature_length = len(f"def {method.get('method')}(")
        pparams = "".join(print_param(method["params"], signature_length))
        method_desc = "\n\t".join(wrap(text=method["description"], width=130, break_long_words=False))

        desc = [
            "\n\t\t".join(wrap(f"{arg['param']}: {arg['description']}", width=130, break_long_words=False)).expandtabs(4)
            for arg in method["params"]
        ]
        params_desc_list = "\n\t\t".join(desc)
        params_description = f"\n\tArgs:\n\t\t{params_desc_list}\n\t" if params_desc_list else ""

        rpc_return_desc = "\n\t\t".join(wrap(method["result"], width=130, break_long_words=False))
        rpc_return = f"Returns:\n\t\t{rpc_return_desc}\n\t" if rpc_return_desc else ""
        if not params_description and rpc_return:
            rpc_return = "\n\t" + rpc_return

        required_params = []
        optional_params = []
        for arg in method["params"]:
            p_str = f"params['{arg['param']}'] = {arg['param']}"
            if arg["required"]:
                required_params.append(p_str)
            else:
                optional_params.append(
                    f"if {arg['param']} is not None:\n\t\t{p_str}".expandtabs(4)
                )

        all_params = []
        all_params.extend(required_params)
        all_params.extend(optional_params)
        start_symbols = "\n\t" if all_params else "\n"

        rpc = rpc_tmpl.replace("$rpc_method", method["method"])
        rpc = rpc.replace("$pparams", pparams)
        rpc = rpc.replace("$rpc_description", method_desc.expandtabs(4))
        rpc = rpc.replace("$rpc_params_description", params_description.expandtabs(4))
        rpc = rpc.replace("$rpc_response", rpc_return.expandtabs(4))
        rpc = rpc.replace("$all_params", (start_symbols + f"\n\t".join(all_params)).expandtabs(4))
        content += f"\n{rpc}"

    stdout.write(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="RPC functions and documentation generator"
    )
    parser.add_argument(
        dest="schema",
        help="path to rpc json schema",
    )

    args = parser.parse_args()

    if not path.exists(args.schema):
        raise FileNotFoundError(f'Cannot access {args.schema}: No such file or directory')

    with open(args.schema, "r") as file:
        schema = load(file)

    generate_rpcs(schema)
