#!/bin/bash

HOST="127.0.0.1"
PORT="3490"
CLIENT_EXEC="./client"
NUM_CLIENTS=10000

echo "Starting $NUM_CLIENTS clients..."

for ((i=1; i<=NUM_CLIENTS; i++))
do
    $CLIENT_EXEC $HOST $PORT &  # Run each client in the background
done

echo "All clients started."
wait 
