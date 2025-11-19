#compile chat_client and chat_server
gcc chat_client.c -o chat_client
gcc chat_server.c -o chat_server

#run chat_server in background
./chat_server &
