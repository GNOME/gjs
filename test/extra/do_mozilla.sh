#!/bin/bash -e

function do_Configure_MozBuild(){
    echo
    echo '-- Set JHBuild Configuration --'

    mkdir -p ~/.config
    autogenargs=""

    if [[ -n "${BUILD_OPTS}" ]]; then
        autogenargs="$autogenargs $BUILD_OPTS"
    fi

    cat <<EOFILE >> ~/.config/jhbuildrc
module_autogenargs['mozjs52'] = "$autogenargs"
module_makeargs['mozjs52'] = '-s'
EOFILE

    echo '-- Done --'
}

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    # Configure the Mozilla build
    do_Configure_MozBuild

    # Build Mozilla Stuff
    jhbuild build mozjs52
}
