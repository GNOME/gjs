#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: Will Thompson

# Automate the release process for stable branches.
# https://blogs.gnome.org/wjjt/2022/06/07/release-semi-automation
# gnome-initial-setup/build-aux/maintainer-upload-release

set -ex
: "${MESON_BUILD_ROOT:?}"
: "${MESON_SOURCE_ROOT:?}"
project_version="${1:?project version is required}"

# Don't forget to write release notes
head -n1 "${MESON_SOURCE_ROOT}/NEWS" | grep "$project_version"

case $project_version in
    1.7[12].*) gnome_series=42 ;;
    1.7[34].*) gnome_series=43 ;;
    1.7[56].*) gnome_series=44 ;;
    1.7[78].*) gnome_series=45 ;;
    1.79.* | 1.80.*) gnome_series=46 ;;
    1.8[12].*) gnome_series=47 ;;
    1.8[34].*) gnome_series=48 ;;
    1.8[56].*) gnome_series=49 ;;
    1.8[78].*) gnome_series=50 ;;
    *)
        echo "Version $project_version not handled by this script"
        exit 1
        ;;
esac
expected_branch=gnome-${gnome_series}

pushd "$MESON_SOURCE_ROOT"
    branch=$(git rev-parse --abbrev-ref HEAD)
    if [[ "$branch" != "master" ]] && [[ "$branch" != "$expected_branch" ]]; then
        echo "Project version $project_version does not match branch $branch" >&2
        exit 1
    fi
    if git show-ref --tags "$project_version" --quiet; then
        # Tag already exists; verify that it points to HEAD
        [ "$(git rev-parse "$project_version"^{})" = "$(git rev-parse HEAD)" ]
    else
        git tag -s "$project_version" -m "Version $project_version"
    fi
    git push --atomic origin "$branch" "$project_version"
popd
