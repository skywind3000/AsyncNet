mode: dll
flag: -O3, -Wall
link: stdc++
int: ../build/objs
unix/link: rt, pthread

win32/flag: -s
unix/flag: -g

out: ../AsyncNet.$(target)

src: ../source/AsyncNet.cpp
src: ../source/TraceLog.cpp

src: ../system/imembase.c
src: ../system/imemdata.c
src: ../system/inetbase.c
src: ../system/inetcode.c
src: ../system/inetkcp.c
src: ../system/inetnot.c
src: ../system/iposix.c
src: ../system/itoolbox.c
src: ../system/ineturl.c
src: ../system/isecure.c

src:
color: 13
echo:       ___                             _   __     __ 
echo:      /   |  _______  ______  _____   / | / /__  / /_
echo:     / /| | / ___/ / / / __ \/ ___/  /  |/ / _ \/ __/
echo:    / ___ |(__  ) /_/ / / / / /__   / /|  /  __/ /_  
echo:   /_/  |_/____/\__, /_/ /_/\___/  /_/ |_/\___/\__/  
echo:               /____/                                
color: -1


