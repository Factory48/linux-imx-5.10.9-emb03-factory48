#!/usr/bin/env python3
"""Migrate the retained EMB03 DTB's audio bindings to NXP 2.6.0, offline.

Only CCM's removed IPG_AUDIO assignment, SDMA2 clocks and enabled SAI
compatibles change. GPIOs, rails, NPU policy and every other property stay put.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

CCM = '/soc@0/bus@30000000/clock-controller@30380000'
AUDIO = '/soc@0/bus@30c00000'
SDMA = AUDIO + '/dma-controller@30e10000'
OLD_IPG = 111
AUDIO_AHB = 108
AUDIO_AHB_ROOT = 315


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def cells(dtb, node, prop):
    return [int(x, 16) for x in run('fdtget', '-t', 'x', dtb, node, prop).split()]


def strings(dtb, node, prop):
    return run('fdtget', '-t', 's', dtb, node, prop).split()


def properties(dtb):
    result, path = {}, []
    for line in run('dtc', '-q', '-I', 'dtb', '-O', 'dts', dtb).splitlines():
        s = line.strip()
        if s.endswith('{'):
            path.append(s[:-1].strip() if s != '/ {' else '')
        elif s == '};':
            path.pop()
        elif s.endswith(';'):
            key = s.split('=', 1)[0].rstrip(' ;')
            result[('/'.join(path) or '/', key)] = s
    return result


def migrate(source, output):
    source, output = Path(source), Path(output)
    if output.exists():
        raise ValueError(f'refusing to overwrite {output}')
    if 'fsl,imx8mp' not in strings(source, '/', 'compatible'):
        raise ValueError('expected an i.MX8MP DTB')
    if cells(source, CCM, '#clock-cells') != [1]:
        raise ValueError('unexpected CCM clock specifier size')
    phandle, = cells(source, CCM, 'phandle')
    before = properties(source)
    changes = {}
    assignments = cells(source, CCM, 'assigned-clocks')
    if len(assignments) % 2 or any(assignments[i] != phandle for i in range(0, len(assignments), 2)):
        raise ValueError('unexpected CCM assigned-clock providers')
    ids = assignments[1::2]
    if ids.count(OLD_IPG) > 1:
        raise ValueError('duplicate removed CCM clock assignment')
    if OLD_IPG in ids:
        index = ids.index(OLD_IPG)
        rates = cells(source, CCM, 'assigned-clock-rates')
        parents = cells(source, CCM, 'assigned-clock-parents')
        if len(rates) != len(ids) or len(parents) % 2 or any(parents[i] != phandle for i in range(0, len(parents), 2)):
            raise ValueError('unexpected CCM assignment geometry')
        changes[(CCM, 'assigned-clocks')] = ('x', assignments[:2 * index] + assignments[2 * index + 2:])
        changes[(CCM, 'assigned-clock-rates')] = ('x', rates[:index] + rates[index + 1:])
        if index < len(parents) // 2:
            changes[(CCM, 'assigned-clock-parents')] = ('x', parents[:2 * index] + parents[2 * index + 2:])
    if strings(source, SDMA, 'compatible') != ['fsl,imx8mp-sdma', 'fsl,imx7d-sdma']:
        raise ValueError('unexpected SDMA2 compatible')
    desired = [phandle, AUDIO_AHB_ROOT, phandle, AUDIO_AHB_ROOT]
    old = cells(source, SDMA, 'clocks')
    if old not in ([phandle, OLD_IPG, phandle, AUDIO_AHB], desired):
        raise ValueError('unexpected SDMA2 clock wiring')
    if old != desired:
        changes[(SDMA, 'clocks')] = ('x', desired)
    for addr in ('30c30000', '30c50000'):
        node = AUDIO + '/spba-bus@30c00000/sai@' + addr
        if strings(source, node, 'status') != ['okay']:
            raise ValueError(f'expected enabled SAI node: {node}')
        old = strings(source, node, 'compatible')
        desired = ['fsl,imx8mp-sai', 'fsl,imx8mm-sai']
        if old not in (['fsl,imx8mq-sai', 'fsl,imx6sx-sai'], desired):
            raise ValueError(f'unexpected SAI compatible: {node}')
        if old != desired:
            changes[(node, 'compatible')] = ('s', desired)
    with tempfile.TemporaryDirectory(prefix='.emb03-dtb-', dir=output.parent) as tmp:
        candidate = Path(tmp) / 'board.dtb'
        shutil.copyfile(source, candidate)
        for (node, prop), (kind, values) in changes.items():
            args = [format(v, 'x') for v in values] if kind == 'x' else values
            subprocess.run(['fdtput', '-t', kind, str(candidate), node, prop, *args], check=True)
        after = properties(candidate)
        actual = {k for k in before.keys() | after.keys() if before.get(k) != after.get(k)}
        if actual != set(changes):
            raise ValueError(f'unexpected DT property changes: {actual ^ set(changes)}')
        # Link publishes a complete file without replacing an existing output.
        os.link(candidate, output)
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    return {'source_sha256': sha(source), 'target_sha256': sha(output),
            'changes': [{'node': n, 'property': p, 'before': before[(n, p)], 'after': after[(n, p)]}
                        for n, p in sorted(changes)], 'all_other_properties_preserved': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(migrate(args.source, args.output), indent=2))
