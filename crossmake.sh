#!/bin/bash

docker run --rm -v $(pwd):/workdir -e CROSS_TRIPLE=arm-linux-gnueabihf multiarch/crossbuild make -j4 $@