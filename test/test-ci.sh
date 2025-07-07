#!/bin/sh -e
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Claudio André <claudioandre.br@gmail.com>

do_Set_Env () {
    #Save cache on $pwd (required by artifacts)
    mkdir -p "$(pwd)"/.cache
    XDG_CACHE_HOME="$(pwd)"/.cache
    export XDG_CACHE_HOME

    #SpiderMonkey and libgjs
    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib64:/usr/local/lib:
    export GI_TYPELIB_PATH=$GI_TYPELIB_PATH:/usr/lib64/girepository-1.0

    #Macros
    export ACLOCAL_PATH=$ACLOCAL_PATH:/usr/local/share/aclocal

    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    export DISPLAY="${DISPLAY:-:0}"
}

do_Get_Upstream_Base () {
    echo '-----------------------------------------'
    echo 'Finding common ancestor'

    if git show-branch ci-upstream-base 2> /dev/null; then
        echo "Already found"
        return
    fi

    # We need to add a new remote for the upstream target branch, since this
    # script could be running in a personal fork of the repository which has out
    # of date branches.
    #
    # Limit the fetch to a certain date horizon to limit the amount of data we
    # get. If the branch was forked from the main branch before this horizon, it
    # should probably be rebased.
    git remote add upstream https://gitlab.gnome.org/GNOME/gjs.git || \
        git remote set-url upstream https://gitlab.gnome.org/GNOME/gjs.git
    # $CI_MERGE_REQUEST_TARGET_BRANCH_NAME is only defined if we’re running in a
    # merge request pipeline; fall back to $CI_DEFAULT_BRANCH otherwise.
    base_branch="${CI_MERGE_REQUEST_TARGET_BRANCH_NAME:-${CI_DEFAULT_BRANCH}}"
    if ! git fetch --shallow-since="28 days ago" --no-tags upstream "$base_branch"; then
        echo "Main branch doesn't have history in the past 28 days, fetching "
        echo "the last 30 commits."
        git fetch --depth=30 --no-tags upstream "$base_branch"
    fi

    git branch -f ci-upstream-base-branch FETCH_HEAD

    # Work out the newest common ancestor between the detached HEAD that this CI
    # job has checked out, and the upstream target branch (which will typically
    # be `upstream/main` or `upstream/gnome-nn`).
    newest_common_ancestor_sha=$(git merge-base ci-upstream-base-branch HEAD)
    if test -z "$newest_common_ancestor_sha"; then
        echo "Couldn’t find common ancestor with the upstream main branch. This"
        echo "typically happens if you branched a long time ago. Please update"
        echo "your clone, rebase, and re-push your branch."
        echo "Base revisions:"
        git log --oneline -10 ci-upstream-base-branch
        echo "Branch revisions:"
        git log --oneline -10 HEAD
        exit 1
    fi
    echo "Merge base:"
    git show --no-patch "$newest_common_ancestor_sha"
    git branch -f ci-upstream-base "$newest_common_ancestor_sha"
    echo '-----------------------------------------'
}

do_Compare_With_Upstream_Base () {
    echo '-----------------------------------------'
    echo 'Comparing the code with upstream merge base'

    sort < /cwd/base-report.txt > /cwd/base-report-sorted.txt
    sort < /cwd/head-report.txt > /cwd/head-report-sorted.txt

    NEW_WARNINGS=$(comm -13 /cwd/base-report-sorted.txt /cwd/head-report-sorted.txt | wc -l)
    REMOVED_WARNINGS=$(comm -23 /cwd/base-report-sorted.txt /cwd/head-report-sorted.txt | wc -l)
    if test "$NEW_WARNINGS" -ne 0; then
        echo '-----------------------------------------'
        echo "### $NEW_WARNINGS new warning(s) found by $1 ###"
        echo '-----------------------------------------'
        diff -U0 /cwd/base-report.txt /cwd/head-report.txt || true
        echo '-----------------------------------------'
        exit 1
    else
        echo "$REMOVED_WARNINGS warning(s) were fixed."
        echo "=> $1 Ok"
    fi
}

do_Create_Artifacts_Folder () {
    # Create the artifacts folders
    save_dir="$(pwd)"
    mkdir -p "$save_dir"/analysis; touch "$save_dir"/analysis/doing-"$1"
}

do_Check_Script_Errors () {
    local total=0
    total=$(cat scripts.log | grep 'not ok ' | wc -l)

    if test "$total" -gt 0; then
        echo '-----------------------------------------'
        echo "### Found $total errors on scripts.log ###"
        echo '-----------------------------------------'
        exit 1
    fi
}

# ----------- Run the Tests -----------
if test -n "$TEST"; then
    extra_opts="($TEST)"
fi

. test/extra/do_environment.sh

# Show some environment info
do_Print_Labels  'ENVIRONMENT'
echo "Running on: $BASE $OS"
echo "Job: $CI_JOB_NAME"
echo "Configure options: ${CONFIG_OPTS-$BUILD_OPTS}"
echo "Doing: $1 $extra_opts"

do_Create_Artifacts_Folder "$1"

# Ignore extra git security checks as we don't care in CI.
git config --global --add safe.directory "${PWD}"
git config --global --add safe.directory \
    "${PWD}/subprojects/gobject-introspection-tests"

if test "$1" = "SETUP"; then
    do_Show_Info
    do_Print_Labels 'Show GJS git information'
    git log --pretty=format:"%h %cd %s" -1

elif test "$1" = "BUILD"; then
    do_Set_Env

    DEFAULT_CONFIG_OPTS="-Dreadline=enabled -Dprofiler=enabled -Ddtrace=false \
        -Dsystemtap=false -Dverbose_logs=false --werror -Dglib:werror=false"
    meson setup _build $DEFAULT_CONFIG_OPTS $CONFIG_OPTS
    ninja -C _build

    if test "$TEST" != "skip"; then
        xvfb-run -a meson test -C _build --suite=gjs $TEST_OPTS
    fi

elif test "$1" = "SH_CHECKS"; then
    # It doesn't (re)build, just run the 'Tests'
    do_Print_Labels 'Shell Scripts Check'
    do_Set_Env

    export LC_ALL=C.UTF-8
    export LANG=C.UTF-8
    export LANGUAGE=C.UTF-8
    export NO_AT_BRIDGE=1

    sudo ninja -C _build install
    installed-tests/scripts/testExamples.sh > scripts.log
    do_Check_Script_Errors

elif test "$1" = "CPPLINT"; then
    do_Print_Labels 'C/C++ Linter report '

    cpplint --quiet --filter=-build/include_what_you_use \
        $(find . -name \*.cpp -or -name \*.h | sort) 2>&1 >/dev/null | \
        tee "$save_dir"/analysis/head-report.txt | \
        sed -E -e 's/:[0-9]+:/:LINE:/' -e 's/  +/ /g' \
        > /cwd/head-report.txt
    cat "$save_dir"/analysis/head-report.txt
    echo

    do_Get_Upstream_Base
    if test $(git rev-parse HEAD) = $(git rev-parse ci-upstream-base); then
        echo '-----------------------------------------'
        echo 'Running against upstream'
        echo '=> cpplint: Nothing to do'
        do_Done
        exit 0
    fi
    git checkout ci-upstream-base
    cpplint --quiet --filter=-build/include_what_you_use \
        $(find . -name \*.cpp -or -name \*.h | sort) 2>&1 >/dev/null | \
        tee "$save_dir"/analysis/base-report.txt | \
        sed -E -e 's/:[0-9]+:/:LINE:/' -e 's/  +/ /g' \
        > /cwd/base-report.txt
    echo

    # Compare the report with merge base and fail if new warnings are found
    do_Compare_With_Upstream_Base "cpplint"

elif test "$1" = "UPSTREAM_BASE"; then
    do_Get_Upstream_Base
    exit 0
fi

# Releases stuff and finishes
do_Done
