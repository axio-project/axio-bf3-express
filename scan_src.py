import sys
import glob

'''
argv[1] - module name
'''
kArgvIndex_module = 1

# add all local source files
sources = glob.glob("./src/**/*.c", recursive=True)     \
        + glob.glob("./src/**/*.cpp", recursive=True)   \
        + glob.glob("./src/**/*.cc", recursive=True)

for i in sources:
    print(i)
