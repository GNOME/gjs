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
    do_Show_Info
    do_Build_Mozilla
    do_Shrink_Image
fi
# Clear the environment
unset BUILD_OPTS
