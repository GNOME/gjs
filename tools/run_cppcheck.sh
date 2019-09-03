#!/bin/sh

ninja -C _build
cppcheck --project=_build/compile_commands.json --inline-suppr \
    --enable=warning,performance,portability,missingInclude \
    --force --quiet $@
