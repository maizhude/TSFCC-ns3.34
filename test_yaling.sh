#!/bin/bash

for ((sendNum = 90; sendNum <=150; sendNum += 10)); do
    sudo ./waf --run "tsfcc-yaling-incast --sendNum=$sendNum --tracing=true --transport_port=TcpCubic --is_sdn=true --delay=10 --bottleneck_delay=10"
done

# for ((sendNum = 12; sendNum <= 52; sendNum += 10)); do
#     sudo ./waf --run "yaling-topo --sendNum=$sendNum --tracing=true --transport_port=TcpCubic --is_sdn=false"
# done

for ((sendNum = 90; sendNum < 150; sendNum += 10)); do
    sudo ./waf --run "tsfcc-yaling-incast --sendNum=$sendNum --tracing=true --transport_port=TcpDctcp --is_sdn=false"
done

paplay /usr/share/sounds/ubuntu/stereo/service-login.ogg