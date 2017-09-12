#!/bin/sh

../../mt/release_mt_env.sh make clean
../../mt/release_mt_env.sh make 
mv ./server.exe ./server_release_mt.exe
