#!/bin/bash -e

IMAGE=fedora:26
BASE=fedora
NAME=fedora.26.gcc
REPO=claudioandre/spidermonkey

docker run -v $(pwd):/cwd -v $(pwd)/test-ci.sh:/test-ci.sh
    -e BASE=$BASE -e OS=$IMAGE -e CC=gcc $IMAGE bash -e -c "/test-ci.sh BUILD_MOZ"
     
docker run --name $NAME -v $(pwd):/cwd -v $(pwd)/test-ci.sh:/test-ci.sh
    -e BASE=$BASE -e OS=$IMAGE -e CC=gcc $IMAGE bash -e -c "/test-ci.sh GET_FILES DOCKER"

docker commit $NAME $REPO:$COMMIT

###########################################################
# script:
#   - cd docker
#   # Creates a much smaller image.
#   - 'docker run -v $(pwd):/cwd -v $(pwd)/test-ci.sh:/test-ci.sh
#       -e BASE=$BASE -e OS=$IMAGE -e CC=gcc $IMAGE bash -e -c "/test-ci.sh BUILD_MOZ"
#     '
#   - 'docker run --name $NAME -v $(pwd):/cwd -v $(pwd)/test-ci.sh:/test-ci.sh
#       -e BASE=$BASE -e OS=$IMAGE -e CC=gcc $IMAGE bash -e -c "/test-ci.sh GET_FILES DOCKER"
#     '
#   - docker commit $NAME $REPO:$COMMIT
# 
#   # Doing a regular build
#   #- docker build --rm -f $FILE -t $REPO:$COMMIT .
# 
# after_success:
#   - export TAG=`if [ "$TRAVIS_BRANCH" == "master" ]; then echo "latest"; else echo $TRAVIS_BRANCH ; fi`
#   - docker tag $REPO:$COMMIT $REPO:$NAME
#   - docker tag $REPO:$COMMIT $REPO:build-$TRAVIS_BUILD_NUMBER
#   - 'if [[ $NAME == "fedora.26.gcc" ]]; then
#        docker tag $REPO:$COMMIT $REPO:$TAG;
#      fi'
#   - docker login -u $DOCKER_USER -p $DOCKER_PASS
#   - docker push $REPO
###########################################################
