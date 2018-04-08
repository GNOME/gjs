#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# heapdot.py - DOT Graph output


import re

func_regex = re.compile('Function(?: ([^/]+)(?:/([<|\w]+))?)?')
gobj_regex = re.compile('([^ ]+) (\(nil\)|0x[a-fA-F0-9]+$)')


###############################################################################
# Shape Compression
###############################################################################

def findi(m, x):
    if not x in m:
        m[x] = [x, 0]
        return m[x]
    if m[x][0] == x:
        return m[x]
    z = findi (m, m[x][0])
    m[x] = z
    return z


def find(m, x):
    return findi(m, x)[0]


def union(m, rep, x, y):
    xp = findi (m, x)
    yp = findi (m, y)
    if xp == yp:
        return
    if xp[1] < yp[1]:
        rep[yp[0]] = rep.get(xp[0], xp[0])
        if xp[0] in rep:
            del rep[xp[0]]
        m[xp[0]][0] = yp[0]
    elif xp[1] > yp[1]:
        m[yp[0]][0] = xp[0]
    else:
        m[yp[0]][0] = xp[0]
        m[xp[0]][1] += 1


def compress_shapes(graph, nodes, edges):
    shape_merge = {}
    shape_rep = {}

    def canon_node(x):
        y = find(shape_merge, x)
        if y in shape_rep:
            y = shape_rep[y]
        return y

    for x in nodes:
        if graph.node_labels.get(x, '') != 'shape':
            continue
        if not x in edges:
            continue
        for y in edges[x]:
            if graph.node_labels.get(y, '') != 'shape' and graph.node_labels.get(y, '') != 'base_shape':
                continue
            union(shape_merge, shape_rep, y, x)
            break

    # Remove merged away nodes
    for x in shape_merge.keys():
        if canon_node(x) != x:
            nodes.remove(x)

    # Update the edges for merging
    new_edges = {}
    for x, dsts in edges.items():
        new_dsts = set([])
        for y in dsts:
            new_dsts.add(canon_node(y))
        x = canon_node(x)
        if x in new_dsts:
            new_dsts.remove(x)
        new_edges[x] = new_edges.get(x, set([])).union(new_dsts)
    edges = new_edges


###############################################################################
# DOT Graph Output
###############################################################################

dot_graph_paths = []


def add_dot_graph_path(path):
    dot_graph_paths.append(path)


def output_dot_file(args, graph, targs, fname):

    # build the set of nodes
    nodes = set([])
    for p in dot_graph_paths:
        for x in p:
            nodes.add(x)

    # build the edge map
    edges = {}

    for p in dot_graph_paths:
        prevNode = None
        for x in p:
            if prevNode:
                edges.setdefault(prevNode, set([])).add(x)
            prevNode = x

    # Shape Compression
    compress_shapes(graph, nodes, edges)


    # Write out the DOT graph
    outf = open(fname, 'w')
    outf.write('digraph {\n')

    # Nodes
    for addr in nodes:
        label = graph.node_labels.get(addr, '')
        color = 'black'
        style = 'solid'
        shape = 'rect'
        native = ''

        if label.endswith('<no private>'):
            label = label[:-13]

        # Lookup the edge label for this node
        elabel = ''

        for origin in graph.edge_labels.values():
            if addr in origin:
                elabels = origin[addr]
                elabel = elabels[0]
                break


        # GObject or something else with a native address
        gm = gobj_regex.match(label)

        if gm:
            label = gm.group(1)
            color = 'orange'
            style = 'bold'

            if not args.no_addr:
                native = gm.group(2)

            # Some kind of GObject
            if label.startswith('GObject_'):
                shape = 'circle'

                if elabel in ['prototype', 'group_proto']:
                    style += ',dashed'
            # Another object native to Gjs
            elif label.startswith('Gjs') or label.startswith('GIR'):
                shape = 'octagon'
        elif label.startswith('Function'):
            fm = func_regex.match(label)

            if fm.group(2) == '<':
                label = 'Function via {}()'.format(fm.group(1))
            elif fm.group(2):
                label = 'Function {} in {}'.format(fm.group(2), fm.group(1))
            else:
                if len(label) > 10:
                    label = label[9:]
                label += '()'

            color = 'green'
            style = 'bold,rounded'
        # A function context
        elif label == 'Call' or label == 'LexicalEnvironment':
            color = 'green'
            style = 'bold,dashed'
        # A file/script reference
        elif label.startswith('script'):
            label = label[7:].split('/')[-1]
            shape = 'note'
            color = 'blue'
        # A WeakMap
        elif label.startswith('WeakMap'):
            label = 'WeakMap'
            style = 'dashed'
        # Mostly uninteresting objects
        elif label in ['base_shape', 'object_group', 'type_object']:
            style = 'dotted'
            if label == 'base_shape':
                label = 'shape'
            elif label == 'type_object':
                label = 'type'

        # Only mark the target if it's a single match
        if addr == targs[0] and len(targs) == 1:
            color = 'red'
            style = 'bold'

        if args.no_addr:
            outf.write('  node [label="{0}", color={1}, shape={2}, style="{3}"] q{4};\n'.format(label, color, shape, style, addr))
        else:
            if native:
                outf.write('  node [label="{0}\\njsobj@{4}\\nnative@{5}", color={1}, shape={2}, style="{3}"] q{4};\n'.format(label, color, shape, style, addr, native))
            else:
                outf.write('  node [label="{0}\\njsobj@{4}", color={1}, shape={2}, style="{3}"] q{4};\n'.format(label, color, shape, style, addr))

    # Edges (relationships)
    for origin, destinations in edges.items():
        for destination in destinations:
            labels = graph.edge_labels.get(origin, {}).get(destination, [])
            ll = []

            for l in labels:
                if len(l) == 2:
                    l = l[0]
                if l.startswith('**UNKNOWN SLOT '):
                    continue
                ll.append(l)

            label = ''
            style = 'solid'
            color = 'black'

            if len(ll) == 1:
                label = ll[0]

                # Object children
                if label.startswith('objects['):
                    label = label[7:]
                # Array elements
                elif label.startswith('objectElements['):
                    label = label[14:]
                # prototype/constructor function
                elif label in ['prototype', 'group_proto']:
                    color = 'orange'
                    style = 'bold,dashed'
                # fun_environment
                elif label == 'fun_environment':
                    label = ''
                    color = 'green'
                    style = 'bold,dashed'
                elif label == 'script':
                    label = ''
                    color = 'blue'
                # Signals
                # TODO: better heap label via gi/closure.cpp & gi/object.cpp
                elif label == 'signal connection':
                    color = 'red'
                    style = 'bold,dashed'

                if len(label) > 18:
                    label = label[:8] + '...' + label[-8:]
            else:
                label = ',\\n'.join(ll)

            outf.write('  q{0} -> q{1} [label="{2}", color={3}, style="{4}"];\n'.format(origin, destination, label, color, style))

    outf.write('}\n')
    outf.close()
