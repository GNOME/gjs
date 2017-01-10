#!/bin/bash -e

function do_Install_Base_Dependencies(){
    echo
    echo '-- Installing Base Dependencies --'

    if [[ $BASE == "ubuntu" ]]; then
        apt-get update -qq

        # Base dependencies
        apt-get -y -qq install build-essential git clang patch > /dev/null
        apt-get -y -qq install autotools-dev autoconf gettext pkgconf autopoint yelp-tools > /dev/null
        apt-get -y -qq install docbook docbook-xsl libtext-csv-perl > /dev/null
        apt-get -y -qq install zlib1g-dev > /dev/null
        apt-get -y -qq install libtool libicu-dev libnspr4-dev > /dev/null

        # JHBuild dependencies management
        apt-get -y -qq install policykit-1 > /dev/null
        apt-get -y -qq install apt-file > /dev/null
        apt-file update

    elif [[ $BASE == "fedora" ]]; then
        dnf -y -q upgrade

        # Base dependencies
        dnf -y -q install @c-development @development-tools redhat-rpm-config gnome-common
        dnf -y -q install pygobject2 dbus-python perl-Text-CSV perl-XML-Parser gettext-devel gtk-doc ninja-build
        dnf -y -q install zlib-devel libffi-devel
        dnf -y -q install libtool libicu-devel nspr-devel
        dnf -y -q install which
    else
        echo
        echo '-- Error: invalid BASE code --'
        exit 1
    fi
}

function do_Install_Dependencies(){
    echo
    echo '-- Installing Dependencies --'

    if [[ $BASE == "ubuntu" ]]; then
        # Testing dependencies
        apt-get -y -qq install gir1.2-gtk-3.0 xvfb gnome-desktop-testing > /dev/null

    elif [[ $BASE == "fedora" ]]; then
        # Testing dependencies
        dnf -y install gtk3 gobject-introspection Xvfb gnome-desktop-testing

        # jhbuild sysdeps: Don't know how to install packages on this system
        dnf -y -q install cairo intltool libxslt bison nspr zlib python3-devel dbus-glib libicu libffi pcre libxml2 libxslt libtool flex
        dnf -y -q install cairo-devel zlib-devel libffi-devel pcre-devel libxml2-devel libxslt-devel
    fi
}

function do_Patch_JHBuild(){
    echo
    echo '-- Patching JHBuild --'

    # Create and apply a patch
    cd jhbuild
    echo "diff --git a/jhbuild/main.py b/jhbuild/main.py"                                                      > what.patch
    echo "index a5cf99b..93410b6 100644"                                                                      >> what.patch
    echo "--- a/jhbuild/main.py"                                                                              >> what.patch
    echo "+++ b/jhbuild/main.py"                                                                              >> what.patch
    echo "@@ -96,7 +96,6 @@ def main(args):"                                                                  >> what.patch
    echo " "                                                                                                  >> what.patch
    echo "     if hasattr(os, 'getuid') and os.getuid() == 0:"                                                >> what.patch
    echo "         sys.stderr.write(_('You should not run jhbuild as root.\n').encode(_encoding, 'replace'))" >> what.patch
    echo "-        sys.exit(1)"                                                                               >> what.patch
    echo " "                                                                                                  >> what.patch
    echo "     logging.getLogger().setLevel(logging.INFO)"                                                    >> what.patch
    echo "     logging_handler = logging.StreamHandler()"                                                     >> what.patch

    echo "diff --git a/jhbuild/utils/systeminstall.py b/jhbuild/utils/systeminstall.py"                       >> what.patch
    echo "index 75b0849..d5d45f0 100644"                                                                      >> what.patch
    echo "--- a/jhbuild/utils/systeminstall.py"                                                               >> what.patch
    echo "+++ b/jhbuild/utils/systeminstall.py"                                                               >> what.patch
    echo "@@ -428,7 +428,7 @@ class AptSystemInstall(SystemInstall):"                                         >> what.patch
    echo " "                                                                                                  >> what.patch
    echo "     def _install_packages(self, native_packages):"                                                 >> what.patch
    echo "         logging.info(_('Installing: %(pkgs)s') % {'pkgs': ' '.join(native_packages)})"             >> what.patch
    echo "-        args = self._root_command_prefix_args + ['apt-get', 'install']"                            >> what.patch
    echo "+        args = ['apt-get', '-y', 'install']"                                                       >> what.patch
    echo "         args.extend(native_packages)"                                                              >> what.patch
    echo "         subprocess.check_call(args)"                                                               >> what.patch

    patch -p1 < what.patch
    cd -
}

function do_Build_JHBuild(){
    echo
    echo '-- Building JHBuild --'

    # Build JHBuild
    cd jhbuild
    git log --pretty=format:"%h %cd %s" -1
    echo
    ./autogen.sh
    make -sj2
    make install
    PATH=$PATH:~/.local/bin
    git reset --hard HEAD
}

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla 31 --'

    # Build Mozilla Stuff
    if [[ -n "$SHELL" ]]; then
        export SHELL=/bin/bash
    fi
    jhbuild build mozjs31
}

function do_Build_Package_Dependencies(){
    echo
    echo "-- Building Dependencies for $1 --"
    jhbuild list $1

    # Build package dependencies
    if [[ $BASE == "ubuntu" ]]; then
        jhbuild sysdeps --install $1
    fi
    jhbuild build $(jhbuild list $1 | sed '$d')
}

function do_Save_Files(){
    echo
    echo '-- Saving build files --'
    mkdir -p /cwd/SAVED/$OS

    cp -r ~/jhbuild /cwd/SAVED/$OS/jhbuild
    cp -r ~/.local  /cwd/SAVED/$OS/.local
    echo '-- Done --'
}

function do_Get_Files(){
    echo
    echo '-- Restoring build files --'

    cp -r /cwd/SAVED/$OS/jhbuild ~/jhbuild
    cp -r /cwd/SAVED/$OS/.local  ~/.local
    echo '-- Done --'
}

function do_Clean_Image(){
    echo
    echo '-- Cleaning image --'

    # Clean the environment
    rm -f /root/jhbuild/install/lib/libmozjs-31.a
    jhbuild clean

    if [[ $BASE == "ubuntu" ]]; then
        # Base dependencies
        apt-get -y -qq remove --purge build-essential git clang patch
        apt-get -y -qq remove --purge autotools-dev autoconf gettext pkgconf autopoint yelp-tools
        apt-get -y -qq remove --purge docbook docbook-xsl libtext-csv-perl
        apt-get -y -qq remove --purge zlib1g-dev
        apt-get -y -qq remove --purge libtool libicu-dev libnspr4-dev

        # JHBuild dependencies management
        apt-get -y -qq remove --purge policykit-1
        apt-get -y -qq remove --purge apt-file

        apt-get -y autoremove
        apt-get -y clean
        rm -rf /var/lib/apt/lists/*

    elif [[ $BASE == "fedora" ]]; then
        # Base dependencies
        dnf -y remove @c-development @development-tools redhat-rpm-config gnome-common
        dnf -y remove pygobject2 dbus-python perl-Text-CSV perl-XML-Parser gettext-devel gtk-doc ninja-build
        dnf -y remove zlib-devel libffi-devel
        dnf -y remove libtool libicu-devel nspr-devel
        dnf -y remove which || true

        dnf -y autoremove
        dnf -y clean all
    fi
    echo
    echo '-- Image done --'
}

# ----------- GJS -----------
cd /cwd

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS"
echo "Doing: $1"

if [[ $1 == "BUILD_MOZ" ]]; then
    do_Install_Base_Dependencies

    # Compiler to be used
    echo
    echo '-- Compiler in use --'
    $CC --version

    do_Patch_JHBuild
    do_Build_JHBuild
    do_Build_Mozilla
    do_Save_Files

elif [[ $1 == "SAVE_MOZ" ]]; then
    do_Get_Files

elif [[ $1 == "GJS" ]]; then
    do_Install_Base_Dependencies
    do_Install_Dependencies

    # Compiler to be used
    echo
    echo '-- Compiler in use --'
    $CC --version

    do_Patch_JHBuild
    do_Build_JHBuild
    do_Build_Package_Dependencies gjs

    # Build the latest commit (merged or from a PR) of Javascript Bindings for GNOME
    echo
    echo '-- gjs build --'
    cd ../gjs
    git log --pretty=format:"%h %cd %s" -1
    echo
    jhbuild run ./autogen.sh --prefix /root/jhbuild/install --enable-compile-warnings=error --enable-installed-tests --with-xvfb-tests
    jhbuild run make -sj2
    jhbuild run make install

    # Test the build
    echo
    echo '-- Testing GJS --'
    gnome-desktop-testing-runner gjs
    echo
    jhbuild run make check

else
    echo
    echo '-- NOTHING TO DO --'
    exit 1
fi

