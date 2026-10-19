#!/usr/bin/env python3
"""Merge decoder_model_quantized.onnx + decoder_with_past_model_quantized.onnx
into a single If-graph decoder (decoder_model_merged_quantized.onnx).

Both files are quantized from the same fp32 weights: every tensor of the
with-past decoder is byte-identical to its counterpart in the full decoder.
We keep one copy of each initializer (outer scope) and wrap the two graphs
in an ONNX If node selected by `use_cache_branch`:

  use_cache_branch=false -> full decoder (step 0, builds KV cache)
  use_cache_branch=true  -> decoder_with_past (steps 1..N)

Result: ~450 MB less on disk and one ORT session instead of two in RAM.
Numerics are unchanged (nodes are copied verbatim).

Usage:
  python3 tools/merge_verbalizer_onnx.py --old-dir models/verbalizer/onnx-int8 \
      --out models/verbalizer/onnx-int8/decoder_model_merged_quantized.onnx

Requires: onnx, numpy.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper


def tensor_sig(t: onnx.TensorProto):
    a = numpy_helper.to_array(t)
    return (t.data_type, tuple(t.dims), hashlib.sha1(np.ascontiguousarray(a).tobytes()).hexdigest())


def build(old_dir: str, out_path: str) -> None:
    full_path = os.path.join(old_dir, "decoder_model_quantized.onnx")
    past_path = os.path.join(old_dir, "decoder_with_past_model_quantized.onnx")
    full = onnx.load(full_path)
    past = onnx.load(past_path)

    f_by_sig = {}
    for t in full.graph.initializer:
        f_by_sig.setdefault(tensor_sig(t), t.name)

    init_rename = {}
    pastonly = []
    for t in past.graph.initializer:
        name = f_by_sig.get(tensor_sig(t))
        if name is None:
            name = "pastonly::" + t.name
            nt = onnx.TensorProto()
            nt.CopyFrom(t)
            nt.name = name
            pastonly.append(nt)
        init_rename[t.name] = name
    print(f"shared tensors: {len(init_rename) - len(pastonly)}, past-only: {len(pastonly)}")

    past_inputs = {i.name for i in past.graph.input}

    def rn(name: str) -> str:
        if name in init_rename:
            return init_rename[name]
        if name in past_inputs:
            return name
        return "then::" + name if name else name

    then_nodes = []
    for n in past.graph.node:
        nn = onnx.NodeProto()
        nn.CopyFrom(n)
        nn.input[:] = [rn(x) for x in n.input]
        nn.output[:] = ["then::" + x for x in n.output]
        then_nodes.append(nn)

    # Branch outputs must match the full decoder positionally; the with-past
    # graph has no cross-attention presents, so pass the inputs through.
    then_outputs = []
    for o in full.graph.output:
        if ".encoder." in o.name:
            out_name = "then::" + o.name
            then_nodes.append(
                helper.make_node(
                    "Identity",
                    [o.name.replace("present.", "past_key_values.")],
                    [out_name],
                )
            )
        else:
            out_name = "then::" + o.name
        vi = onnx.ValueInfoProto()
        vi.CopyFrom(o)
        vi.name = out_name
        then_outputs.append(vi)

    then_graph = helper.make_graph(then_nodes, "then_branch", [], then_outputs)
    else_graph = helper.make_graph(
        list(full.graph.node),
        "else_branch",
        [],
        [onnx.ValueInfoProto.FromString(o.SerializeToString()) for o in full.graph.output],
    )

    outer_inputs = [onnx.ValueInfoProto.FromString(i.SerializeToString()) for i in full.graph.input]
    full_inputs = {i.name for i in full.graph.input}
    for i in past.graph.input:
        if i.name not in full_inputs:
            outer_inputs.append(onnx.ValueInfoProto.FromString(i.SerializeToString()))
    outer_inputs.append(helper.make_tensor_value_info("use_cache_branch", TensorProto.BOOL, [1]))

    if_node = helper.make_node(
        "If",
        ["use_cache_branch"],
        [o.name for o in full.graph.output],
        then_branch=then_graph,
        else_branch=else_graph,
    )
    graph = helper.make_graph(
        [if_node],
        "merged_decoder",
        outer_inputs,
        [onnx.ValueInfoProto.FromString(o.SerializeToString()) for o in full.graph.output],
        initializer=list(full.graph.initializer) + pastonly,
    )
    model = helper.make_model(graph, opset_imports=list(full.opset_import))
    model.ir_version = full.ir_version
    model.producer_name = "holos-merge-verbalizer"
    onnx.save(model, out_path, save_as_external_data=False)
    print(f"saved {out_path} ({os.path.getsize(out_path) / 1e6:.1f} MB)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old-dir", default="models/verbalizer/onnx-int8",
                    help="dir with decoder_model_quantized.onnx + decoder_with_past_model_quantized.onnx")
    ap.add_argument("--out", default="models/verbalizer/onnx-int8/decoder_model_merged_quantized.onnx")
    args = ap.parse_args()
    build(args.old_dir, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
