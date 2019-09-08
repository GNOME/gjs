#!/bin/bash -e

function do_Shrink_Image(){
    echo
    echo '-- Cleaning image --'
    PATH=$PATH:~/.local/bin
    rm -rf ~/jhbuild/install/lib/libjs_static.ajs

    dnf -y clean all
    rm -rf /var/cache/dnf

    echo '-- Done --'
}

if [[ $1 == "BUILD_MOZ" ]]; then
    do_Install_Dependencies
    do_Set_Env

    if [[ $DEV == "devel" ]]; then
        do_Install_Extras
    fi
    do_Show_Info
    do_Get_JHBuild
    do_Build_JHBuild
    do_Build_Mozilla

    # Build JHBuild to create a docker image ready to go
    jhbuild build m4-common

    do_Shrink_Image
fi
# Clear the environment
unset BUILD_OPTS
