#!/bin/bash -e

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    # Build Mozilla Stuff
    jhbuild build mozjs52
}
