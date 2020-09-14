#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Endless Mobile, Inc.

if ! which locale > /dev/null; then
  exit 1
fi

locales=$(locale -a | xargs -n1)

case $locales in
  # Prefer C.UTF-8 although it is only available with newer libc
  *C.UTF-8*) tests_locale=C.UTF-8 ;;
  # C.utf8 has also been observed in the wild
  *C.utf8*) tests_locale=C.utf8 ;;

  # Most systems will probably have this
  *en_US.UTF-8*) tests_locale=en_US.UTF-8 ;;
  *en_US.utf8*) tests_locale=en_US.utf8 ;;

  # If not, fall back to any English UTF-8 locale or any UTF-8 locale at all
  *en_*.UTF-8*) tests_locale=$(echo $locales | grep -m1 en_.\*\\.UTF-8) ;;
  *en_*.utf8*) tests_locale=$(echo $locales | grep -m1 en_.\*\\.utf8) ;;
  *.UTF-8*) tests_locale=$(echo $locales | grep -m1 \\.UTF-8) ;;
  *.utf8*) tests_locale=$(echo $locales | grep -m1 \\.utf8) ;;

  *) tests_locale=C ;;
esac

echo $tests_locale
