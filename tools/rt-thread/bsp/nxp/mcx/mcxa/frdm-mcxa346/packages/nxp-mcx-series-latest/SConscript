import os
from building import *

objs = []
cwd  = GetCurrentDir()

if GetDepend(['SOC_MCXA346']):
    objs = objs + SConscript('MCXA346/SConscript')

if GetDepend(['SOC_MCXA366']):
    objs = objs + SConscript('MCXA366/SConscript')

if GetDepend(['SOC_MCXA344']):
    objs = objs + SConscript('MCXA344/SConscript')

if GetDepend(['SOC_MCXA156']):
    objs = objs + SConscript('MCXA156/SConscript')

if GetDepend(['SOC_MCXA153']):
    objs = objs + SConscript('MCXA153/SConscript')

if GetDepend(['SOC_MCXC444']):
    objs = objs + SConscript('MCXC444/SConscript')

if GetDepend(['SOC_MCXC162']):
    objs = objs + SConscript('MCXC162/SConscript')

if GetDepend(['SOC_MCXE247']):
    objs = objs + SConscript('MCXE247/SConscript')

if GetDepend(['SOC_MCXN236']):
    objs = objs + SConscript('MCXN236/SConscript')

if GetDepend(['SOC_MCXN947']):
    objs = objs + SConscript('MCXN947/SConscript')

Return('objs')
