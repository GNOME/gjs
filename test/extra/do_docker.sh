#!/bin/bash -e

function do_Shrink_Image(){
    echo
    echo '-- Cleaning image --'
    PATH=$PATH:~/.local/bin
    rm -rf ~/jhbuild/install/lib/libjs_static.ajs

    if [[ $BASE == "ubuntu" ]]; then
        apt-get -y clean
        rm -rf /var/lib/apt/lists/*

    elif [[ $BASE == "fedora" ]]; then
        dnf -y clean all
    fi

    echo '-- Done --'
}

if [[ $STATIC == "analysis" ]]; then
    do_Install_Analyser
    do_Shrink_Image

elif [[ $STATIC == "flatpak" ]]; then
    do_Install_Base_Dependencies
    do_Install_Extras
    do_Shrink_Image

elif [[ $1 == "BUILD_MOZ" ]]; then
    do_Install_Base_Dependencies
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
