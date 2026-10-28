# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 Alejandro Bustos
"""Check the parsed Turtle against src/ports.hpp. Test dependency: rdflib."""
import pathlib
import re
import sys

try:
    import rdflib
    from rdflib import Namespace, RDF, URIRef
except ImportError:
    sys.exit("FAIL ttl_check: install python3-rdflib (or pip install rdflib)")

ROOT = pathlib.Path(__file__).resolve().parents[1]
LV2 = Namespace("http://lv2plug.in/ns/lv2core#")
DOAP = Namespace("http://usefulinc.com/ns/doap#")
RDFS = Namespace("http://www.w3.org/2000/01/rdf-schema#")
PLUGIN = URIRef("https://github.com/ABustos2606/octant#octant")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(bundle):
    header = (ROOT / "src/ports.hpp").read_text(encoding="utf-8")
    body = re.search(r"enum class Port : uint32_t \{(.*?)\};", header, re.S)
    require(body is not None, "missing Port")
    declared = re.findall(r"(\w+)\s*=\s*(\d+),\s*//\s*(audio_in|audio_out|control_in|control_out)", body[1])
    expected = {int(index): (symbol, kind) for symbol, index, kind in declared}
    count = int(re.search(r"count\s*=\s*(\d+)", body[1])[1])
    require(len(declared) == len(expected) == count and set(expected) == set(range(count)), "Port indices")
    manifest = rdflib.Graph().parse(bundle / "manifest.ttl", format="turtle")
    require(set(manifest.subjects(RDF.type, LV2.Plugin)) == {PLUGIN}, "manifest plugins")
    require(manifest.value(PLUGIN, LV2.binary) == URIRef((bundle / "octant.so").as_uri()), "binary")
    graph = rdflib.Graph().parse(bundle / "octant.ttl", format="turtle")
    require((PLUGIN, RDF.type, LV2.PitchPlugin) in graph, "class")
    require(graph.value(PLUGIN, DOAP.license) == URIRef("https://spdx.org/licenses/GPL-2.0-or-later"), "license")
    require(not set(graph.objects(PLUGIN, LV2.requiredFeature)), "no required features")
    require(str(graph.value(PLUGIN, DOAP.name)).isascii(), "name")
    require(str(graph.value(PLUGIN, RDFS.comment)).isascii(), "description")
    actual = {}
    for port in graph.objects(PLUGIN, LV2.port):
        index, symbol, name = int(graph.value(port, LV2["index"])), str(graph.value(port, LV2.symbol)), str(graph.value(port, LV2.name))
        require(index not in actual, f"duplicate index {index}")
        require(name.isascii() and 0 < len(name) <= 16, f"{symbol}: name must be ASCII, at most 16 characters")
        types = set(graph.objects(port, RDF.type))
        if LV2.AudioPort in types:
            kind = "audio_in" if LV2.InputPort in types else "audio_out"
        elif LV2.OutputPort in types:
            require(LV2.ControlPort in types, f"{symbol}: control output")
            kind = "control_out"
        else:
            require(LV2.ControlPort in types and LV2.InputPort in types, f"{symbol}: control input")
            lo, hi, default = (graph.value(port, p) for p in (LV2.minimum, LV2.maximum, LV2.default))
            require(None not in (lo, hi, default) and float(lo) <= float(default) <= float(hi), f"{symbol}: bounds and default")
            for point in graph.objects(port, LV2.scalePoint):
                label = str(graph.value(point, RDFS.label))
                require(label.isascii() and 0 < len(label) <= 16, f"{symbol}: value names must be ASCII, at most 16 characters")
            kind = "control_in"
        actual[index] = (symbol, kind)
    require(actual == expected, f"ports differ from Port: {actual}")
    print(f"PASS ttl_check: {count} ports match Port; audio ports, value names, license, features")


if __name__ == "__main__":
    try:
        check(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "octant.lv2").resolve())
    except Exception as error:
        sys.exit(f"FAIL ttl_check: {error}")
