#!/bin/bash -e

function do_Save_Files(){
    echo
    echo '-- Saving build files --'
    mkdir -p "/cwd/SAVED/$OS"

    cp -r ~/jhbuild "/cwd/SAVED/$OS/jhbuild"
    cp -r ~/.local  "/cwd/SAVED/$OS/.local"
    echo '-- Done --'
}

function do_Get_Files(){
    echo
    echo '-- Restoring build files --'

    cp -r "/cwd/SAVED/$OS/jhbuild" ~/jhbuild
    cp -r "/cwd/SAVED/$OS/.local"  ~/.local
    echo '-- Done --'
}
