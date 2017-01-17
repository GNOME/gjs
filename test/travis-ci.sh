#!/bin/bash -e

function do_Install_Base_Dependencies(){
    echo
    echo '-- Installing Base Dependencies --'

    if [[ $BASE == "ubuntu" ]]; then
        apt-get update -qq

        # Base dependencies
        apt-get -y -qq install build-essential git clang patch \
                               autotools-dev autoconf gettext pkgconf autopoint yelp-tools \
                               docbook docbook-xsl libtext-csv-perl \
                               zlib1g-dev \
                               libtool libicu-dev libnspr4-dev \
                               policykit-1 \
                               apt-file > /dev/null
        apt-file update

    elif [[ $BASE == "fedora" ]]; then
        dnf -y -q upgrade

        # Base dependencies
        dnf -y -q install @c-development @development-tools redhat-rpm-config gnome-common \
                          pygobject2 dbus-python perl-Text-CSV perl-XML-Parser gettext-devel gtk-doc ninja-build \
                          zlib-devel libffi-devel \
                          libtool libicu-devel nspr-devel
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
        apt-get -y -qq install gir1.2-gtk-3.0 xvfb gnome-desktop-testing dbus-x11 dbus > /dev/null

    elif [[ $BASE == "fedora" ]]; then
        # Testing dependencies
        dnf -y -q install gtk3 gobject-introspection Xvfb gnome-desktop-testing dbus-x11 dbus \
                          cairo intltool libxslt bison nspr zlib python3-devel dbus-glib libicu libffi pcre libxml2 libxslt libtool flex \
                          cairo-devel zlib-devel libffi-devel pcre-devel libxml2-devel libxslt-devel
    fi
}

function do_Set_Env(){
    echo
    echo '-- Set Environment --'

    if [[ -z $DISPLAY ]]; then
        export DISPLAY=":0"
    fi

    echo '-- Done --'
}

function do_Patch_JHBuild(){
    echo
    echo '-- Patching JHBuild --'

    # Create and apply a patch
    cd jhbuild
    patch -p1 <<ENDPATCH
diff --git a/jhbuild/main.py b/jhbuild/main.py
index a5cf99b..28c31d6 100644
--- a/jhbuild/main.py
+++ b/jhbuild/main.py
@@ -94,9 +94,9 @@ def main(args):
         localedir = None
     gettext.install('jhbuild', localedir=localedir, unicode=True)
 
-    if hasattr(os, 'getuid') and os.getuid() == 0:
-        sys.stderr.write(_('You should not run jhbuild as root.\n').encode(_encoding, 'replace'))
-        sys.exit(1)
+    # if hasattr(os, 'getuid') and os.getuid() == 0:
+    #    sys.stderr.write(_('You should not run jhbuild as root.\n').encode(_encoding, 'replace'))
+    #    sys.exit(1)
 
     logging.getLogger().setLevel(logging.INFO)
     logging_handler = logging.StreamHandler()
diff --git a/jhbuild/utils/systeminstall.py b/jhbuild/utils/systeminstall.py
index 75b0849..08965fa 100644
--- a/jhbuild/utils/systeminstall.py
+++ b/jhbuild/utils/systeminstall.py
@@ -428,7 +428,7 @@ class AptSystemInstall(SystemInstall):
 
     def _install_packages(self, native_packages):
         logging.info(_('Installing: %(pkgs)s') % {'pkgs': ' '.join(native_packages)})
-        args = self._root_command_prefix_args + ['apt-get', 'install']
+        args = ['apt-get', '-y', 'install']
         args.extend(native_packages)
         subprocess.check_call(args)
ENDPATCH

    echo '-- Done --'
    cd -
}

function do_Configure_JHBuild(){
    echo
    echo '-- Set JHBuild Configuration --'

    mkdir -p ~/.config

    cat <<EOFILE >> ~/.config/jhbuildrc
module_autogenargs['gjs'] = '--enable-compile-warnings=error --enable-installed-tests --with-xvfb-tests'
module_makeargs['gjs'] = '-s'
EOFILE

    echo '-- Done --'
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

function do_Show_Compiler(){

    if [[ ! -z $CC ]]; then
        echo
        echo '-- Compiler in use --'
        $CC --version
    fi
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

    do_Show_Compiler
    do_Patch_JHBuild
    do_Build_JHBuild
    do_Build_Mozilla
    do_Save_Files

elif [[ $1 == "GET_FILES" ]]; then
    do_Get_Files

elif [[ $1 == "GJS" ]]; then
    do_Install_Base_Dependencies
    do_Install_Dependencies
    do_Set_Env

    do_Show_Compiler
    do_Patch_JHBuild
    do_Build_JHBuild
    do_Configure_JHBuild
    do_Build_Package_Dependencies gjs

    # Build and test the latest commit (merged or from a PR) of Javascript Bindings for GNOME
    echo
    echo '-- gjs build --'
    cp -r ../gjs ~/jhbuild/checkout/gjs
    cd ~/jhbuild/checkout/gjs
    git log --pretty=format:"%h %cd %s" -1
    echo
    jhbuild make --check

    # Extra testing
    echo
    echo '-- Installed GJS tests --'
    jhbuild run dbus-run-session -- gnome-desktop-testing-runner gjs

else
    echo
    echo '-- NOTHING TO DO --'
    exit 1
fi
# Done
echo
echo '-- DONE --'

