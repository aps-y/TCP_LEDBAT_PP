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

sudo rmmod tcp_ledbat