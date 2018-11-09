#!/bin/sh

cd kmd
./build.sh

cd ..

nvdla_docker_id=$(sudo docker container ls | grep 'nvdla/vp' | awk '{print $1}')
echo $nvdla_docker_id
#if [ $# = 0 ]; then
#    echo 'default mode, restarting ...'
#fi
if [ $# = 1 ] && [ $1 = "update" ]; then
    nvdla_docker_num=$( echo "$nvdla_docker_id" | wc -w)
    echo $nvdla_docker_num
    if [ "$nvdla_docker_num" = "1" ]; then
        echo 'copying...'
        sudo docker cp kmd/port/linux/opendla.ko $nvdla_docker_id:/usr/local/nvdla/opendla.ko
        exit 0
    else
        echo 'more than one nvdla, restarting ...'
    fi
fi

#exit 0

if [ -z "$nvdla_docker_id" ];
then
    echo 'no running container'
else
    echo 'stopping running container'
    sudo docker stop $nvdla_docker_id
    echo 'deleting stopped container'
    sudo docker rm $nvdla_docker_id
fi
gnome-terminal -- sudo docker run -it -v /home:/home nvdla/vp
nvdla_docker_id=''
while [ -z "$nvdla_docker_id" ];
do
    echo 'maybe you should enter you sudo password in another terminal ...'
    echo 'sleep 1s ...'
    sleep 1
    echo 'searching nvdla docker container'
    nvdla_docker_id=$(sudo docker container ls | grep 'nvdla/vp' | awk '{print $1}')
done
echo "new nvdla container: $nvdla_docker_id"

echo 'copying umd files'
sudo docker cp umd/out/runtime/libnvdla_runtime/libnvdla_runtime.so $nvdla_docker_id:/usr/local/nvdla/libnvdla_runtime.so
sudo docker cp umd/out/runtime/nvdla_runtime/nvdla_runtime $nvdla_docker_id:/usr/local/nvdla/nvdla_runtime

echo 'copying added to container...'
sudo docker cp added $nvdla_docker_id:/usr/local/nvdla

echo 'copying opendla.ko to container...'
sudo docker cp kmd/port/linux/opendla.ko $nvdla_docker_id:/usr/local/nvdla/opendla.ko

echo 'copying Image, rootfs, drm,ko to container...'
sudo docker cp ~/buildroot/output/images/Image $nvdla_docker_id:/usr/local/nvdla/Image
sudo docker cp ~/buildroot/output/images/rootfs.ext2 $nvdla_docker_id:/usr/local/nvdla/rootfs.ext4
sudo docker cp ~/buildroot/output/build/linux-4.13.3/drivers/gpu/drm/drm.ko $nvdla_docker_id:/usr/local/nvdla/drm.ko

echo 'done'
