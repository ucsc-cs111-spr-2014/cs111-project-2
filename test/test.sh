#!/bin/sh

test_equal_cpu() {
    echo "Testing two cs_cpu with equal time and tickets"
    time ./cs_cpu $1 &
    time ./cs_cpu $1 &
}

test_double_cpu() {
    echo "Testing two cs_cpu with equal time but with 20 and 40"
    echo "tickets"
    time cs_cpu $1 &
    time nice -n 20 cs_cpu $1 &
}

test_triple_cpu() {
    echo "Testing three cs_cpu with equal time" 
    echo "but with 25, 50, and 100 tickets"

    time nice -n 5 cs_cpu 5 &
    time nice -n 30 cs_cpu 5 &
    time nice -n 80 cs_cpu 5 &
}

test_starve_cpu() {
    echo "Running several cs_cpu with 100 tickets" 
    echo "and one with 1 ticket"

    time nice -n 80 cs_cpu 5 &
    time nice -n 80 cs_cpu 5 &
    time nice -n 80 cs_cpu 5 &
    time nice -n 80 cs_cpu 5 &
    time nice -n -19 cs_cpu 5 &
}

# echo "Showing that dynamic scheduler dynamically adjusts tickets in" 
# echo "processes"

# echo "Showing that dynamic scheduler improves performance when"
# echo "mixing CPU and IO bound tasks compared to a fixed number of 
# echo "tickets"

case $1 in
    -e)
        test_equal_cpu $2
        ;;
    -d)
        test_double_cpu $2
        ;;
    -t)
        test_triple_cpu $2
        ;;
    -s)
        test_starve_spu $2
        ;;    
esac
