cd src
make
sudo make install
cd ..

read -p "Which test you want to run 1.Test1 2.Test2 " choice

if [ $choice = "1" ]
then 
    ./test1.sh
    echo "Test1 is loaded successfully!"
fi
if [ $choice = "2" ]
then 
    ./test2.sh
    echo "Test2 is loaded successfully!"
fi
