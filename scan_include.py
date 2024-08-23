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
    if "__template__" in i or "__TEMPLATE__" in i:
        continue
    print(i)
