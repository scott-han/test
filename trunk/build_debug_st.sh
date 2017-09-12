#!/bin/sh

../../mk/debug_st_env.sh make clean
../../mk/debug_st_env.sh make 
mv ./server.exe ./server_debug_st.exe
