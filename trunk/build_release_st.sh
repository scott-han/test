#!/bin/sh

../../mt/release_st_env.sh make clean
../../mt/release_st_env.sh make 
mv ./server.exe ./server_release_st.exe
