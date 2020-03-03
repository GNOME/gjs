#!/bin/sh -e

do_Set_Env () {
    #Save cache on $pwd (required by artifacts)
    mkdir -p "$(pwd)"/.cache
    XDG_CACHE_HOME="$(pwd)"/.cache
    export XDG_CACHE_HOME

    #SpiderMonkey and libgjs
    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib64:/usr/local/lib

    #Macros
    export ACLOCAL_PATH=$ACLOCAL_PATH:/usr/local/share/aclocal

    export JHBUILD_RUN_AS_ROOT=1
    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    export DISPLAY="${DISPLAY:-:0}"
}

do_Get_Upstream_Master () {
    if test "$CI_PROJECT_PATH_SLUG" = "gnome-gjs"; then
        if test "$CI_BUILD_REF_SLUG" = "master" -o \
            "$CI_BUILD_REF_SLUG" = "gnome-"* -o \
             -n "$CI_COMMIT_TAG"; then
            echo '-----------------------------------------'
            echo 'Running against upstream'
            echo "=> $1 Nothing to do"

            do_Done
            exit 0
        fi
    fi

    echo '-----------------------------------------'
    echo 'Cloning upstream master'

    mkdir -p ~/tmp-upstream; cd ~/tmp-upstream || exit 1
    git clone --depth 1 https://gitlab.gnome.org/GNOME/gjs.git; cd gjs || exit 1
    echo '-----------------------------------------'
}

do_Compare_With_Upstream_Master () {
    echo '-----------------------------------------'
    echo 'Compare the working code with upstream master'

    sort < /cwd/master-report.txt > /cwd/master-report-sorted.txt
    sort < /cwd/current-report.txt > /cwd/current-report-sorted.txt

    NEW_WARNINGS=$(comm -13 /cwd/master-report-sorted.txt /cwd/current-report-sorted.txt | wc -l)
    REMOVED_WARNINGS=$(comm -23 /cwd/master-report-sorted.txt /cwd/current-report-sorted.txt | wc -l)
    if test "$NEW_WARNINGS" -ne 0; then
        echo '-----------------------------------------'
        echo "### $NEW_WARNINGS new warning(s) found by $1 ###"
        echo '-----------------------------------------'
        diff -U0 /cwd/master-report.txt /cwd/current-report.txt || true
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
    total=$(cat scripts.log | grep 'not ok ' | awk '{total+=1}END{print total}')

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
echo "Job: $TASK_ID"
echo "Configure options: ${CONFIG_OPTS-$BUILD_OPTS}"
echo "Doing: $1 $extra_opts"

do_Create_Artifacts_Folder "$1"

if test "$1" = "SETUP"; then
    do_Show_Info
    do_Print_Labels 'Show GJS git information'
    git log --pretty=format:"%h %cd %s" -1

elif test "$1" = "BUILD"; then
    do_Set_Env

    DEFAULT_CONFIG_OPTS="-Dcairo=enabled -Dreadline=enabled -Dprofiler=enabled \
        -Ddtrace=false -Dsystemtap=false -Dverbose_logs=false --werror"
    meson _build $DEFAULT_CONFIG_OPTS $CONFIG_OPTS
    ninja -C _build

    if test "$TEST" != "skip"; then
        xvfb-run -a meson test -C _build $TEST_OPTS
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

    cpplint --quiet $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 >/dev/null | \
        tee "$save_dir"/analysis/current-report.txt | \
        sed -E -e 's/:[0-9]+:/:LINE:/' -e 's/  +/ /g' \
        > /cwd/current-report.txt
    cat "$save_dir"/analysis/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master "cppLint"
    cpplint --quiet $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 >/dev/null | \
        tee "$save_dir"/analysis/master-report.txt | \
        sed -E -e 's/:[0-9]+:/:LINE:/' -e 's/  +/ /g' \
        > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "cppLint"
fi

# Releases stuff and finishes
do_Done
