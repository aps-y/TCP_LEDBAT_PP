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

sudo rmmod tcp_ledbat