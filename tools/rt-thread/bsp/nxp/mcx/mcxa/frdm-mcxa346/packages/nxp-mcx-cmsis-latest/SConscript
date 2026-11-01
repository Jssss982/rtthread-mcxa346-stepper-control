Import('rtconfig')
from building import *

cwd = GetCurrentDir()
path = [cwd + '/Core/Include']
src = []


group = DefineGroup('Libraries', src, depend = [''], CPPPATH = path)

Return('group')
