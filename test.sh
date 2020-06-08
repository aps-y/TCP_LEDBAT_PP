Netns_Name=$(ip netns | grep -o snd)
if [ $Netns_Name = "snd" ]
then 
    sudo ip netns del snd
fi

Netns_Name=$(ip netns | grep -o router)
if [ $Netns_Name = "router" ]
then 
    sudo ip netns del router
fi

Netns_Name=$(ip netns | grep -o recv)
if [ $Netns_Name = "recv" ]
then 
    sudo ip netns del recv
fi

Netns_Name=$(ip netns | grep -o snbg)
if [ $Netns_Name = "snbg" ]
then 
    sudo ip netns del snbg
fi

sudo ip netns add recv
sudo ip netns add snd
sudo ip netns add router
sudo ip netns add snbg

sudo ip netns exec snbg sysctl -w net.ipv4.tcp_congestion_control=ledbat

sudo ip link add ethSRo type veth peer name ethRoS
sudo ip link add ethReRo type veth peer name ethRoRe
sudo ip link add ethSbRo type veth peer name ethRoSb

sudo ip link set ethSRo netns snd
sudo ip link set ethRoS netns router
sudo ip link set ethRoRe netns router
sudo ip link set ethRoSb netns router
sudo ip link set ethReRo netns recv
sudo ip link set ethSbRo netns snbg

sudo ip netns exec snd ip link set ethSRo up
sudo ip netns exec router ip link set ethRoS up
sudo ip netns exec router ip link set ethRoRe up
sudo ip netns exec router ip link set ethRoSb up
sudo ip netns exec recv ip link set ethReRo up 
sudo ip netns exec snbg ip link set ethSbRo up


sudo ip netns exec snd ip address add 10.0.1.1/24 dev ethSRo
sudo ip netns exec router ip address add 10.0.1.2/24 dev ethRoS
sudo ip netns exec router ip address add 10.0.2.2/24 dev ethRoRe
sudo ip netns exec router ip address add 10.0.3.2/24 dev ethRoSb
sudo ip netns exec snbg ip address add 10.0.3.1/24 dev ethSbRo
sudo ip netns exec recv ip address add 10.0.2.1/24 dev ethReRo

sudo ip netns exec snd ip link set lo up
sudo ip netns exec router ip link set lo up
sudo ip netns exec recv ip link set lo up
sudo ip netns exec snbg ip link set lo up

sudo ip netns exec snd ip route add default via 10.0.1.2 dev ethSRo
sudo ip netns exec recv ip route add default via 10.0.2.2 dev ethReRo
sudo ip netns exec snbg ip route add default via 10.0.3.2 dev ethSbRo

sudo ip netns exec router sysctl -w net.ipv4.ip_forward=1

sudo ip netns exec snd tc qdisc add dev ethSRo root tbf limit 1000m rate 1000mbps burst 100m
sudo ip netns exec snbg tc qdisc add dev ethSbRo root tbf limit 1000m rate 1000mbps burst 100m

sudo ip netns exec router tc qdisc add dev ethRoRe root handle 1:0 netem delay 70ms 60ms
sudo ip netns exec router tc qdisc add dev ethRoRe parent 1:1 handle 10:0 tbf limit 500m rate 50mbps burst 50m
sudo ip netns exec router tc qdisc add dev ethRoRe parent 10:1 handle 100:0 netem loss 0.01%

# sudo ip netns exec router tc qdisc add dev ethRoRe root handle 1:0 tbf limit 500m rate 50mbps burst 50m
# udo ip netns exec router tc qdisc add dev ethRoRe parent 1:1 handle 10:0 netem loss 0.01%

#snd 10.0.1._
#snbg 10.0.3._
#recv 10.0.2._
#router 10.0._.2