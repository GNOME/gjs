#!/bin/bash -e

function do_Patch_JHBuild(){
    echo
    echo '-- Patching JHBuild --'

    if [[ ! -d jhbuild ]]; then
      git clone --depth 1 https://github.com/GNOME/jhbuild.git
    fi

    # Create and apply a patch
    cd jhbuild
    patch -p1 <<ENDPATCH
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

    cat <<EOFILE > ~/.config/jhbuildrc
skip = ['gettext', 'yelp-xsl', 'yelp-tools', 'gtk-doc']
use_local_modulesets = True
EOFILE

    echo '-- Done --'
}

function do_Configure_MainBuild(){
    echo
    echo '-- Set Main JHBuild Configuration --'

    mkdir -p ~/.config
    autogenargs="--enable-compile-warnings=error --with-xvfb-tests"

    if [[ -n "${BUILD_OPTS}" ]]; then
        autogenargs="$autogenargs $BUILD_OPTS"
    fi

    cat <<EOFILE > ~/.config/jhbuildrc
module_autogenargs['gjs'] = "$autogenargs"
module_makeargs['gjs'] = '-s'
skip = ['gettext', 'yelp-xsl', 'yelp-tools', 'gtk-doc']
use_local_modulesets = True
disable_Werror = False
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

    if [[ $1 == "RESET" ]]; then
        git reset --hard HEAD
    fi
    echo '-- Done --'
    cd -
}
