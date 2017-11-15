#!/usr/bin/env bash

ARGS="-it --privileged --cap-add=ALL"
VOLUME=$HOME/Documents/Git/ipads-labs
if [ $# -eq 1 ] ; then
    VOLUME="$1"
fi

sudo docker run ${ARGS} -v "${VOLUME}:/home/stu/devlop" ddnirvana/cselab_env:latest /bin/bash
