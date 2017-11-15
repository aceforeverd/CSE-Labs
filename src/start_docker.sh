#!/usr/bin/env bash

ARGS="-it --privileged --cap-add=ALL"
DIR=$(dirname "$0")
if [ $# -eq 1 ] ; then
    DIR="$1"
fi
VOLUME=$(realpath "$DIR")

sudo docker run ${ARGS} -v "${VOLUME}:/home/stu/devlop" ddnirvana/cselab_env:latest /bin/bash
