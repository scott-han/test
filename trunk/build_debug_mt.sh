#!/bin/sh

../../mt/debug_mt_env.sh make clean
../../mt/debug_mt_env.sh make 
mv ./server.exe ./server_debug_mt.exe
