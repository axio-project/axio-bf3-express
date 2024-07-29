import sys
import glob


'''
argv[1] - module name
'''
kArgvIndex_module = 1

# add all local include files
include_dirs = [
    "include",
]

for i in include_dirs:
    print(i)
