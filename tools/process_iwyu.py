# coding: utf8
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

# IWYU is missing the feature to designate a certain header as a "forward-decls
# header". In the case of SpiderMonkey, there are certain commonly used forward
# declarations that are all gathered in js/TypeDecls.h.
# We postprocess the IWYU output to fix this, and also fix the output format
# which is quite verbose, making it tedious to scroll through for 60 files.

import re
import sys


class Colors:
    NORMAL = '\33[0m'
    RED = '\33[31m'
    GREEN = '\33[32m'


ADD, REMOVE, FULL = range(3)
state = None
file = None
add = {}
remove = {}
all_includes = {}
there_were_errors = False

# When encountering one of these lines, move to a different state
MATCHERS = {
    r'\.\./(.*) should add these lines:': ADD,
    r'\.\./(.*) should remove these lines:': REMOVE,
    r'The full include-list for \.\./(.*):': FULL,
    r'\(\.\./(.*) has correct #includes/fwd-decls\)': None
}

FWD_HEADER = '#include <js/TypeDecls.h>'
FWD_DECLS_IN_HEADER = (
    'class JSAtom;',
    'struct JSContext;',
    'struct JSClass;',
    'class JSFunction;',
    'class JSObject;',
    'struct JSRuntime;',
    'class JSScript;',
    'class JSString;',
    'struct JSPrincipals;',
    'namespace js { class TempAllocPolicy; }',
    'namespace JS { class GCContext; }',
    'namespace JS { class PropertyKey; }',
    'namespace JS { class Symbol; }',
    'namespace JS { class BigInt; }',
    'namespace JS { class Value; }',
    'namespace JS { class Compartment; }',
    'namespace JS { class Realm; }',
    'namespace JS { struct Runtime; }',
    'namespace JS { class Zone; }',
)
add_fwd_header = False

CPP20_VERSION = '#include <version>'
CSTDDEF = '#include <cstddef>'

FALSE_POSITIVES = (
    # The bodies of these structs already come before their usage,
    # we don't need to have forward declarations of them as well
    ('gjs/atoms.h', 'class GjsAtoms;', ''),
    ('gjs/atoms.h', 'struct GjsSymbolAtom;', ''),
    ('gjs/mem-private.h', 'namespace Gjs { namespace Memory { struct Counter; } }', ''),

    # https://github.com/include-what-you-use/include-what-you-use/issues/1685
    # False positive when constructing JS::GCHashMap
    ('gi/object.h', '#include <utility>', 'for move'),
    ('gjs/jsapi-util-error.cpp', '#include <utility>', 'for forward, move'),
    # False positibe when using JS::WeakCache::put
    ('gi/fundamental.cpp', '#include <utility>', 'for forward'),
    ('gi/gtype.cpp', '#include <utility>', 'for forward'),
    # Same underlying problem, false positive due to inlined methods from
    # gi/info.h and gi/auto.h
    ('gi/interface.cpp', '#include <girepository/girepository.h>', 'for gi_base_info_ref'),
    ('gjs/byteArray.cpp', '#include <girepository/girepository.h>', 'for gi_base_info_get_name, gi_bas...'),
    ('modules/console.cpp', '#include <glib-object.h>', 'for g_object_unref'),

    # For some reason IWYU wants these with angle brackets when they are
    # already present with quotes
    # https://github.com/include-what-you-use/include-what-you-use/issues/1087
    ('gjs/profiler.cpp', '#include <gjs/profiler.h>', ''),

    # IWYU is not sure whether <utility> or <iterator> is for pair
    # https://github.com/include-what-you-use/include-what-you-use/issues/1616
    ('gi/object.cpp', '#include <iterator>', 'for pair'),
    ('gi/toggle.h', '#include <iterator>', 'for pair'),
    ('test/gjs-test-utils.h', '#include <iterator>', 'for pair'),
    ('test/gjs-test-toggle-queue.cpp', '#include <iterator>', 'for pair'),

    # https://github.com/include-what-you-use/include-what-you-use/issues/1831
    ('gi/value.h', 'class ObjectBox;', ''),
)


def output():
    global file, state, add_fwd_header, there_were_errors

    if add_fwd_header:
        if FWD_HEADER not in all_includes:
            if FWD_HEADER in remove:
                remove.pop(FWD_HEADER, None)
            else:
                add[FWD_HEADER] = ''

    # https://github.com/include-what-you-use/include-what-you-use/issues/1791
    if add.pop(CPP20_VERSION, None) is not None and CSTDDEF not in all_includes:
        if CSTDDEF in remove:
            remove.pop(CSTDDEF, None)
        else:
            add[CSTDDEF] = 'for nullptr_t'

    if add or remove:
        print(f'\n== {file} ==')
        for line, why in add.items():
            if why:
                why = '  // ' + why
            print(f'{Colors.GREEN}+{line}{Colors.NORMAL}{why}')
        for line, why in remove.items():
            if why:
                why = '  // ' + why
            print(f'{Colors.RED}-{line}{Colors.NORMAL}{why}')
        there_were_errors = True

    add.clear()
    remove.clear()
    all_includes.clear()
    add_fwd_header = False


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue

    if 'fatal error:' in line:
        print(line)
        there_were_errors = True
        continue

    # filter out errors having to do with compiler arguments unknown to IWYU
    if line.startswith('error:'):
        continue

    if line == '---':
        output()
        continue

    state_changed = False
    file_changed = False
    for matcher, newstate in MATCHERS.items():
        match = re.match(matcher, line)
        if match:
            state = newstate
            if match.group(1) != file:
                if file is not None:
                    file_changed = True
                file = match.group(1)
            state_changed = True
            break
    if file_changed:
        output()
        continue
    if state_changed:
        continue

    line, _, why = line.partition(' // ')
    line = line.strip()
    if state == ADD:
        if line in FWD_DECLS_IN_HEADER:
            add_fwd_header = True
            continue
        if (file, line, why) in FALSE_POSITIVES:
            continue
        add[line] = why
    elif state == REMOVE:
        if line.startswith('- '):
            line = line[2:]
        remove[line] = why
    elif state == FULL:
        all_includes[line] = why

if there_were_errors:
    sys.exit(1)
