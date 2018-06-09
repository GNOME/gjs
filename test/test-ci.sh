#!/bin/bash -e

function do_Print_Labels(){

    if [[ -n "${1}" ]]; then
        label_len=${#1}
        span=$(((54 - $label_len) / 2))

        echo
        echo "= ======================================================== ="
        printf "%s %${span}s %s %${span}s %s\n" "=" "" "$1" "" "="
        echo "= ======================================================== ="
    else
        echo "= ========================= Done ========================= ="
        echo
    fi
}

function do_Set_Env(){

    do_Print_Labels 'Set Environment '

    #Save cache on $pwd (required by artifacts)
    mkdir -p "$(pwd)"/.cache
    XDG_CACHE_HOME="$(pwd)"/.cache
    export XDG_CACHE_HOME

    #SpiderMonkey
    export PKG_CONFIG_PATH=/root/jhbuild/install/lib/pkgconfig
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/jhbuild/install/lib

    #Macros
    export ACLOCAL_PATH=$ACLOCAL_PATH:/root/jhbuild/install/share/aclocal

    export JHBUILD_RUN_AS_ROOT=1
    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    if [[ -z "${DISPLAY}" ]]; then
        export DISPLAY=":0"
    fi

    do_Print_Labels
}

function do_Done(){

    # Done. De-initializes whatever is needed
    do_Print_Labels  'FINISHED'
}

function do_Build_Package_Dependencies(){

    do_Print_Labels "Building Dependencies for $1"
    jhbuild list "$1"

    # Build package dependencies
    jhbuild build $(jhbuild list "$1" | sed '$d')
}

function do_Get_Upstream_Master(){

    if [[ "$CI_PROJECT_PATH_SLUG" == "gnome-gjs" && ("$CI_BUILD_REF_SLUG" == "master" || "$CI_BUILD_REF_SLUG" == "gnome-"*) ]]; then
        echo '-----------------------------------------'
        echo 'Running against upstream'
        echo "=> $1 Nothing to do"

        do_Done
        exit 0
    fi

    echo '-----------------------------------------'
    echo 'Cloning upstream master'

    mkdir -p ~/tmp-upstream; cd ~/tmp-upstream || exit 1
    git clone --depth 1 https://gitlab.gnome.org/GNOME/gjs.git; cd gjs || exit 1
    echo '-----------------------------------------'
}

function do_Compare_With_Upstream_Master(){

    echo '-----------------------------------------'
    echo 'Compare the working code with upstream master'

    NEW_WARNINGS=$(comm -13 <(sort < /cwd/master-report.txt) <(sort < /cwd/current-report.txt) | wc -l)
    REMOVED_WARNINGS=$(comm -23 <(sort < /cwd/master-report.txt) <(sort < /cwd/current-report.txt) | wc -l)
    if test "$NEW_WARNINGS" -ne 0; then
        echo '-----------------------------------------'
        echo "### $NEW_WARNINGS new warning(s) found by $1 ###"
        echo '-----------------------------------------'
        diff -u0 /cwd/master-report.txt /cwd/current-report.txt || true
        echo '-----------------------------------------'
        exit 3
    else
        echo "$REMOVED_WARNINGS warning(s) were fixed."
        echo "=> $1 Ok"
    fi
}

function do_Create_Artifacts_Folder(){

    # Create the artifacts folders
    save_dir="$(pwd)"

    if [[ $1 == "GJS_COVERAGE" ]]; then
         mkdir -p "$save_dir"/coverage; touch "$save_dir"/coverage/doing-"$1"
    fi
    mkdir -p "$save_dir"/analysis; touch "$save_dir"/analysis/doing-"$1"
}

function do_Get_Commit_Message(){

    # Allow CI to skip jobs. Its goal is to simplify housekeeping.
    # Disable tasks using the commit message. Possibilities are (and/or):
    # [skip eslint]		[skip cpplint]		[skip cppcheck]
    #
    # Just add the "code" anywhere inside the commit message.
    log_message=$(git log -n 1)
}

function do_Check_Warnings(){

    cat compilation.log | grep "warning:" | awk '{total+=1}END{print "Total number of warnings: "total}'
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

source test/extra/do_environment.sh

do_Create_Artifacts_Folder "$1"
do_Get_Commit_Message

if [[ $1 == "GJS" ]]; then
    do_Set_Env
    do_Show_Info

    if [[ "$DEV" == "jhbuild" ]]; then
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
    do_Print_Labels 'Show GJS git information'
    git log --pretty=format:"%h %cd %s" -1

    do_Print_Labels 'Do the GJS build'

    if [[ "$DEV" == "jhbuild" ]]; then
        cp -r ./ ~/jhbuild/checkout/gjs
        cd ~/jhbuild/checkout/gjs

        jhbuild make --check
    else
        export AM_DISTCHECK_CONFIGURE_FLAGS="--enable-compile-warnings=yes --with-xvfb-tests"

        # Regular (autotools only) build
        echo "Autogen options: $ci_autogenargs"
        eval ./autogen.sh "$ci_autogenargs"

        make -sj 2>&1 | tee compilation.log

        if [[ $TEST == "distcheck" ]]; then
            make -s distcheck
        elif [[ $TEST == "check" ]]; then
            make -s check
        fi
        make -sj install
    fi

    if [[ $WARNINGS == "count" ]]; then
        do_Print_Labels 'Warnings Report '
        do_Check_Warnings
        do_Print_Labels
    fi

elif [[ $1 == "GJS_EXTRA" ]]; then
    # It doesn't (re)build, just run the 'Installed Tests'
    do_Print_Labels 'Run GJS installed tests'
    do_Set_Env

    if [[ "$DEV" == "jhbuild" ]]; then
        xvfb-run -a --server-args="-screen 0 1024x768x24" jhbuild run dbus-run-session -- gnome-desktop-testing-runner gjs
    else
        xvfb-run -a --server-args="-screen 0 1024x768x24" dbus-run-session -- gnome-desktop-testing-runner gjs
    fi

elif [[ $1 == "VALGRIND" ]]; then
    # It doesn't (re)build, just run the 'Valgrind Tests'
    do_Print_Labels 'Valgrind Report'
    do_Set_Env

    make check-valgrind

elif [[ $1 == "GJS_COVERAGE" ]]; then
    # It doesn't (re)build, just run the 'Coverage Tests'
    do_Print_Labels 'Code Coverage Report'
    do_Set_Env

    jhbuild run --in-builddir=gjs make check-code-coverage
    cp "$(pwd)"/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage.info "$save_dir"/coverage/
    cp -r "$(pwd)"/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage/* "$save_dir"/coverage/

    echo '-----------------------------------------'
    sed -e 's/<[^>]*>//g' "$(pwd)"/coverage/index.html | tr -d ' \t' | grep -A3 -P '^Lines:$'  | tr '\n' ' '; echo
    echo '-----------------------------------------'

elif [[ $1 == "CPPCHECK" && "$log_message" != *'[skip cppcheck]'* ]]; then
    do_Print_Labels 'Static code analyzer report '

    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        tee "$save_dir"/analysis/current-report.txt | sed -E 's/:[0-9]+]/:LINE]/g' > /cwd/current-report.txt
    cat "$save_dir"/analysis/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master "cppCheck"
    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        tee "$save_dir"/analysis/master-report.txt | sed -E 's/:[0-9]+]/:LINE]/g' > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "cppCheck"

elif [[ $1 == "CPPLINT"  && "$log_message" != *'[skip cpplint]'* ]]; then
    do_Print_Labels 'C/C++ Linter report '

    cpplint --quiet $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 | \
        tee "$save_dir"/analysis/current-report.txt | sed -E 's/:[0-9]+:/:LINE:/' > /cwd/current-report.txt
    cat "$save_dir"/analysis/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master "cppLint"
    cpplint --quiet $(find . -name \*.cpp -or -name \*.c -or -name \*.h | sort) 2>&1 | \
        tee "$save_dir"/analysis/master-report.txt | sed -E 's/:[0-9]+:/:LINE:/' > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "cppLint"

elif [[ $1 == "ESLINT" && "$log_message" != *'[skip eslint]'* ]]; then
    do_Print_Labels 'Javascript Linter report'

    tmp_path=$(dirname "$CI_PROJECT_DIR")

    eslint examples installed-tests modules --format unix 2>&1 | \
        tee "$save_dir"/analysis/current-report.txt | \
        sed -E -e 's/:[0-9]+:[0-9]+:/:LINE:COL:/' -e 's/[0-9]+ problems//' -e 's/\/root\/tmp-upstream//' -e "s,$tmp_path,," \
        > /cwd/current-report.txt
        cat "$save_dir"/analysis/current-report.txt
    echo

    # Get the code committed at upstream master
    do_Get_Upstream_Master "esLint"
    cp "$save_dir"/.eslint* .
    eslint examples installed-tests modules --format unix 2>&1 | \
        tee "$save_dir"/analysis/master-report.txt | \
        sed -E -e 's/:[0-9]+:[0-9]+:/:LINE:COL:/' -e 's/[0-9]+ problems//' -e 's/\/root\/tmp-upstream//' -e "s,$tmp_path,," \
        > /cwd/master-report.txt
    echo

    # Compare the report with master and fail if new warnings are found
    do_Compare_With_Upstream_Master "esLint"

elif [[ $1 == "TOKEI" ]]; then
    do_Print_Labels 'Project statistics'

    tokei . | tee "$save_dir"/analysis/report.txt

elif [[ $1 == "FLATPAK" ]]; then
    do_Print_Labels 'Flatpak packaging'

    # Move the manifest file to the root folder
    cp test/*.json .

    # Ajust to the current branch
    sed -i "s,<<ID>>,$APPID,g" ${MANIFEST_PATH}
    sed -i "s,<<master>>,master,g" ${MANIFEST_PATH}
    sed -i "s,<<current>>,origin/$CI_COMMIT_REF_NAME,g" ${MANIFEST_PATH}

    flatpak-builder --bundle-sources --repo=devel build ${MANIFEST_PATH}
    flatpak build-bundle devel ${BUNDLE} --runtime-repo=${RUNTIME_REPO} ${APPID}
fi

# Releases stuff and finishes
do_Done
