#2 routers in series

Netns_Name=$(ip netns | grep -o snd)
if [ $Netns_Name = "snd" ]
then 
    sudo ip netns del snd
fi

Netns_Name=$(ip netns | grep -o router1)
if [ $Netns_Name = "router1" ]
then 
    sudo ip netns del router1
fi

Netns_Name=$(ip netns | grep -o router2)
if [ $Netns_Name = "router2" ]
then 
    sudo ip netns del router2
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
sudo ip netns add router1
sudo ip netns add router2
sudo ip netns add snbg

sudo ip netns exec snbg sysctl -w net.ipv4.tcp_congestion_control=ledbat

sudo ip link add ethSR1 type veth peer name ethR1S
sudo ip link add ethR1R2 type veth peer name ethR2R1
sudo ip link add ethReR2 type veth peer name ethR2Re
sudo ip link add ethSbR1 type veth peer name ethR1Sb

sudo ip link set ethSR1 netns snd
sudo ip link set ethR1S netns router1
sudo ip link set ethR1R2 netns router1
sudo ip link set ethR2R1 netns router2
sudo ip link set ethR2Re netns router2
sudo ip link set ethReR2 netns recv
sudo ip link set ethR1Sb netns router1
sudo ip link set ethSbR1 netns snbg

sudo ip netns exec snd ip link set ethSR1 up
sudo ip netns exec router1 ip link set ethR1S up
sudo ip netns exec router2 ip link set ethR2Re up
sudo ip netns exec router1 ip link set ethR1Sb up
sudo ip netns exec recv ip link set ethReR2 up 
sudo ip netns exec snbg ip link set ethSbR1 up
sudo ip netns exec router1 ip link set ethR1R2 up
sudo ip netns exec router2 ip link set ethR2R1 up


sudo ip netns exec snd ip address add 10.0.1.1/24 dev ethSR1
sudo ip netns exec router1 ip address add 10.0.1.2/24 dev ethR1S
sudo ip netns exec router2 ip address add 10.0.2.2/24 dev ethR2Re
sudo ip netns exec recv ip address add 10.0.2.1/24 dev ethReR2
sudo ip netns exec router1 ip address add 10.0.3.2/24 dev ethR1Sb
sudo ip netns exec snbg ip address add 10.0.3.1/24 dev ethSbR1
sudo ip netns exec router1 ip address add 10.0.4.1/24 dev ethR1R2
sudo ip netns exec router2 ip address add 10.0.4.2/24 dev ethR2R1


sudo ip netns exec snd ip link set lo up
sudo ip netns exec router1 ip link set lo up
sudo ip netns exec router2 ip link set lo up
sudo ip netns exec recv ip link set lo up
sudo ip netns exec snbg ip link set lo up

sudo ip netns exec snd ip route add default via 10.0.1.2 dev ethSR1
sudo ip netns exec recv ip route add default via 10.0.2.2 dev ethReR2
sudo ip netns exec snbg ip route add default via 10.0.3.2 dev ethSbR1
sudo ip netns exec router1 ip route add default via 10.0.4.2 dev ethR1R2
sudo ip netns exec router2 ip route add default via 10.0.4.1 dev ethR2R1

sudo ip netns exec router1 sysctl -w net.ipv4.ip_forward=1
sudo ip netns exec router2 sysctl -w net.ipv4.ip_forward=1

sudo ip netns exec snd tc qdisc add dev ethSR1 root tbf limit 1000m rate 1000mbps burst 100m
sudo ip netns exec snbg tc qdisc add dev ethSbR1 root tbf limit 1000m rate 1000mbps burst 100m

sudo ip netns exec router1 tc qdisc add dev ethR1R2 root handle 1:0 netem delay 70ms 60ms
sudo ip netns exec router1 tc qdisc add dev ethR1R2 parent 1:1 handle 10:0 tbf limit 500m rate 50mbps burst 50m
sudo ip netns exec router1 tc qdisc add dev ethR1R2 parent 10:1 handle 100:0 netem loss 0.01%

# sudo ip netns exec router tc qdisc add dev ethRoRe root tbf limit 500m rate 50mbps burst 50m

#snd 10.0.1._
#snbg 10.0.3._
#recv 10.0.2._
#router 10.0._.2