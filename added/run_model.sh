#!/bin/sh


cd /mnt
./added/init_dla.sh
./nvdla_runtime --loadable added/nvdla_alexnet.nvdla --image added/dog8.jpg --normalize 1.0 --rawdump
#./nvdla_runtime --loadable added/nvdla_alexnet.nvdla --image added/regression/images/digits/eight.pgm --normalize 1.0 --rawdump

#./nvdla_runtime --loadable added/regression/flatbufs/kmd/NN/NN_L0_1_small_fbuf --image added/regression/images/digits/eight.pgm --normalize 1.0 --rawdump
