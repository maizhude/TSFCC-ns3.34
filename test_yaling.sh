#!/bin/bash

for ((sendNum = 12; sendNum <=12; sendNum += 40)); do
    sudo ./waf --run "yaling-topo --sendNum=$sendNum --tracing=true --transport_port=TcpCubic --is_sdn=true"
done

# for ((sendNum = 12; sendNum <= 52; sendNum += 10)); do
#     sudo ./waf --run "yaling-topo --sendNum=$sendNum --tracing=true --transport_port=TcpCubic --is_sdn=false"
# done

# for ((sendNum = 12; sendNum <= 72; sendNum += 10)); do
#     sudo ./waf --run "yaling-topo --sendNum=$sendNum --tracing=true --transport_port=TcpDctcp --is_sdn=false"
# done

paplay /usr/share/sounds/ubuntu/stereo/service-login.ogg