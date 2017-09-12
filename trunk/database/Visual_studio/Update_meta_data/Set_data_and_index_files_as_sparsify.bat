@echo off
rem please copy this batch file into database_root folder, and then start 
rem a cmd window and navigate to database_root, and run this batch file.
rem once it is completed, all files (data, _0_data, _1_data and _2_data)
rem should all be marked as sparse file.

for /d /r %%f in (*) do (
echo "topic folder: %%f"
echo "set flag for file: %%f\data"
fsutil sparse setflag "%%f\data"
fsutil sparse queryflag "%%f\data"
echo "set flag for file: %%f\index_0_data"
fsutil sparse setflag "%%f\index_0_data"
fsutil sparse queryflag "%%f\index_0_data"
echo "set flag for file: %%f\index_1_data"
fsutil sparse setflag "%%f\index_1_data"
fsutil sparse queryflag "%%f\index_1_data"
echo "set flag for file: %%f\index_2_data"
fsutil sparse setflag "%%f\index_2_data"
fsutil sparse queryflag "%%f\index_2_data"
)