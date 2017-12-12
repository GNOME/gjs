#!/bin/bash -e

function do_Shrink_Image(){
    echo
    echo '-- Cleaning image --'
    PATH=$PATH:~/.local/bin
    jhbuild clean || true
    rm -rf ~/bin/jhbuild ~/.local/bin/jhbuild ~/.cache/jhbuild
    rm -rf ~/.config/jhbuildrc ~/.jhbuildrc ~/checkout
    rm -rf ~/jhbuild/checkout
    rm -rf ~/jhbuild/install/lib/libjs_static.ajs

    if [[ $BASE == "ubuntu" ]]; then
        apt-get -y -qq remove --purge apt-file

        apt-get -y autoremove
        apt-get -y clean
        rm -rf /var/lib/apt/lists/*

    elif [[ $BASE == "fedora" ]]; then
        dnf -y autoremove
        dnf -y clean all
    fi

    echo '-- Done --'
}
