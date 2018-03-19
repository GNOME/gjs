#!/bin/bash -e

function do_Set_Env(){
    echo
    echo '-- Set Environment --'

    #Save cache on $pwd (required by artifacts)
    mkdir -p "$(pwd)"/.cache
    XDG_CACHE_HOME="$(pwd)"/.cache
    export XDG_CACHE_HOME
    cp -r /cwd/.cache "$(pwd)"/.cache

    export JHBUILD_RUN_AS_ROOT=1
    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    if [[ -z "${DISPLAY}" ]]; then
        export DISPLAY=":0"
    fi

    echo '-- Done --'
}

function do_Build_Package_Dependencies(){
    echo
    echo "-- Building Dependencies for $1 --"
    jhbuild list "$1"

    # Build package dependencies
    jhbuild build $(jhbuild list "$1" | sed '$d')
}

function do_Show_Info(){

    echo '--------------------------------'
    echo 'Useful build system information'
    id; uname -a
    printenv
    echo '--------------------------------'
    cat /etc/*-release
    echo '--------------------------------'

    if [[ ! -z $CC ]]; then
        echo 'Compiler version'
        $CC --version
        echo '--------------------------------'
        $CC -dM -E -x c /dev/null
        echo '--------------------------------'
    fi
}

function do_Get_Upstream_Master(){

    echo '--------------------------------'
    echo 'Cloning upstream master'

    mkdir -p ~/tmp-upstream; cd ~/tmp-upstream || exit 1
    git clone --depth 1 https://gitlab.gnome.org/GNOME/gjs.git; cd gjs || exit 1
    echo '--------------------------------'
}

function do_Compare_With_Upstream_Master(){

    echo '--------------------------------'
    echo 'Compare the working code with upstream master'

    NEW_WARNINGS=$(comm -13 <(sort < /cwd/master-report.txt) <(sort < /cwd/current-report.txt))
    if test -n "$NEW_WARNINGS"; then
        echo '----------------------------------------'
        echo "###  New warnings found by $1  ###"
        echo '----------------------------------------'
        diff -U0 -u /cwd/master-report.txt /cwd/current-report.txt || true
        echo '----------------------------------------'
        exit 3
    else
        echo "=> $1 Ok"
    fi
}

# ----------- Run the Tests -----------
if [[ -n "${BUILD_OPTS}" ]]; then
    extra_opts="($BUILD_OPTS)"
fi

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS  $extra_opts"
echo "Doing: $1"

source test/extra/do_jhbuild.sh

# Create the artifacts folders
save_dir="$(pwd)"
mkdir -p "$save_dir"/coverage; touch "$save_dir"/coverage/doing-"$1"
mkdir -p "$save_dir"/cppcheck; touch "$save_dir"/cppcheck/doing-"$1"
mkdir -p "$save_dir"/cpplint; touch "$save_dir"/cpplint/doing-"$1"
mkdir -p "$save_dir"/eslint; touch "$save_dir"/eslint/doing-"$1"
mkdir -p "$save_dir"/tokei; touch "$save_dir"/tokei/doing-"$1"

if [[ $1 == "GJS" ]]; then
    do_Set_Env
    do_Show_Info

    if [[ $2 != "devel" ]]; then
        do_Get_JHBuild
        do_Build_JHBuild
        do_Configure_JHBuild
        do_Build_Package_Dependencies gjs

    else
        mkdir -p ~/jhbuild/checkout/gjs
    fi
    do_Configure_MainBuild

    # Build and test the latest commit (merged or from a merge/pull request) of
    # Javascript Bindings for GNOME (gjs)
    echo
    echo '-- gjs status --'
    cp -r ./ ~/jhbuild/checkout/gjs

    cd ~/jhbuild/checkout/gjs
    git log --pretty=format:"%h %cd %s" -1

    echo
    echo '-- gjs build --'
    echo
    jhbuild make --check

elif [[ $1 == "GJS_EXTRA" ]]; then
    # Extra testing. It doesn't (re)build, just run the 'Installed Tests'
    echo
    echo '-- Installed GJS tests --'
    do_Set_Env
    PATH=$PATH:~/.local/bin

    xvfb-run jhbuild run dbus-run-session -- gnome-desktop-testing-runner gjs

elif [[ $1 == "GJS_COVERAGE" ]]; then
    # Code coverage test. It doesn't (re)build, just run the 'Coverage Tests'
    echo
    echo '-- Code Coverage Report --'
    do_Set_Env
    PATH=$PATH:~/.local/bin

    jhbuild run --in-builddir=gjs make check-code-coverage
    cp "$(pwd)"/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage.info "$save_dir"/coverage/
    cp -r "$(pwd)"/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage/* "$save_dir"/coverage/

    echo '-----------------------------------------------------------------'
    sed -e 's/<[^>]*>//g' "$(pwd)"/coverage/index.html | tr -d ' \t' | grep -A3 -P '^Lines:$'  | tr '\n' ' '; echo
    echo '-----------------------------------------------------------------'

elif [[ $1 == "CPPCHECK" ]]; then
    echo
    echo '-- Static code analyzer report --'
    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        tee "$save_dir"/cppcheck/current-report.txt | sed -E 's/:[0-9]+]/:LINE]/' > /cwd/current-report.txt
    cat "$save_dir"/cppcheck/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master

    echo '-- Master static code analyzer report --'
    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        tee "$save_dir"/cppcheck/master-report.txt | sed -E 's/:[0-9]+]/:LINE]/' > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "cppCheck"

elif [[ $1 == "CPPLINT" ]]; then
    # Install needed packages
    pip install cpplint

    echo
    echo '-- Lint report --'
    cpplint $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 | \
        tee "$save_dir"/cpplint/current-report.txt | sed -E 's/:[0-9]+:/:LINE:/' | head -n -1 > /cwd/current-report.txt
    cat "$save_dir"/cpplint/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master

    echo '-- Master Lint report --'
    cpplint $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 | \
        tee "$save_dir"/cpplint/master-report.txt | sed -E 's/:[0-9]+:/:LINE:/' | head -n -1 > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "cppLint"

elif [[ $1 == "ESLINT" ]]; then
    # Install needed packages
    npm install -g eslint
    tmp_path=$(dirname $CI_PROJECT_DIR)

    echo
    echo '-- Javascript linter report --'
    eslint examples installed-tests modules --format unix 2>&1 | \
        tee "$save_dir"/eslint/current-report.txt | \
        sed -E -e 's/:[0-9]+:[0-9]+:/:LINE:COL:/' -e 's/[0-9]+ problems//' -e 's/\/root\/tmp-upstream//' -e "s,$tmp_path,," \
        > /cwd/current-report.txt
        cat "$save_dir"/eslint/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master
    cp "$save_dir"/.eslint* .

    echo '-- Master Javascript linter report --'
    eslint examples installed-tests modules --format unix 2>&1 | \
        tee "$save_dir"/eslint/master-report.txt | \
        sed -E -e 's/:[0-9]+:[0-9]+:/:LINE:COL:/' -e 's/[0-9]+ problems//' -e 's/\/root\/tmp-upstream//' -e "s,$tmp_path,," \
        > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "esLint"

elif [[ $1 == "TOKEI" ]]; then
    echo
    echo '-- Project statistics --'
    echo

    tokei . | tee "$save_dir"/tokei/report.txt
fi
# Done
echo
echo '-- DONE --'
