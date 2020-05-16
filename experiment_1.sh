#TERMINAL 1

ip netns exec router wireshark

#TERMINAL 2

ip netns exec snbg wireshark

#TERMINAL 3

sudo ip netns exec recv iperf -s

#TERMINAL 4

sudo ip netns exec snd iperf -c 10.0.2.1 -t 30 -i 3

#TERMINAL 5

sudo ip netns exec snbg iperf -c 10.0.2.1 -t 30 -i 3


