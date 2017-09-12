import os.path;
import os;
import shutil;
import sys;
"""
This script is to upgrade server's log file structure from version 2.x.x.x to new version (pending).
Basically it just moves files from all sub-folders (current version of logs structure) to parent folder (new version of log file structure).

It is supposed to be used when we upgrade from old version of Synapse server to the new version of Synapse server.
The old version Synapse server has the following log directory structure:
parentFolder/YYYY/YYYYMM/YYYYMMDD_log.txt
But the new log directory structure is a flat one,
parentFolder/YYYYMMDD_log.txt.

The script will move all YYYYMMDD_log.txt files from sub-folders of parentFolder to parentFolder.
It also provides option to move other files from sub-folder to parentFolder.
It also provides option to remove all sub-folders.
"""

if (len(sys.argv) < 2):
    print("Usage:\n")
    print("Python MoveFile.py ParentFolder\n");
    sys.exit(0);

# To get parent folder from command line paremeter.
parentFolder = sys.argv[1];
# To remove space at the begin/end of the string.
parentFolder = parentFolder.strip();
# To check if the parentFolder is valid or not.
if (len(parentFolder) == 0):
    print("Parent folder is invalid.");
    sys.exit(0);
# To check if the parentFolder is existed or not.
if (not os.path.exists(parentFolder)):
    print("Parent folder doesn't exist.");
    sys.exit(0);

#parentFolder = "e:\logs";
total = 0;
moved = 0;
all_subs = [];
remains = [];
for r, s, f in os.walk(parentFolder):
    if (r == parentFolder and s != None):
        for sub_folder in s:
            all_subs.append(os.path.join(r, sub_folder));
    for x in f:
        fullPath = os.path.join(r, x);
        if os.path.isfile(fullPath):
            total += 1;
            dummy, fileName = os.path.split(fullPath);
            if (dummy != parentFolder and fileName.endswith("_log.txt")):
                moved += 1;
                os.rename(fullPath, os.path.join(parentFolder, fileName));
            else:
                if (dummy != parentFolder):
                    remains.append(fullPath);

print("Summary ...");
print("===============================================================================");
print("Total files: " + str(total) + ", moved: " + str(moved)); 
if (total > moved):
    print("The following files are not moved:");
    for x in remains:
        print(x);

    print("Do you want to move them as well (Y/N)?");
    moveAll = raw_input();
    moveAll = moveAll.upper();
    if (moveAll == "Y" or moveAll == "YES"):
        for x in remains:
            dummy, fileName = os.path.split(x);
            os.rename(x, os.path.join(parentFolder, fileName));
        print("All files are moved.");

print("===============================================================================");
delAll = raw_input("Do you want to remove all sub-folders (Y/N)?");
delAll = delAll.upper();
if (delAll == "Y" or delAll == "YES"):
    for folder in all_subs:
        shutil.rmtree(folder);