
CC=gcc -O3 -Wall -Winline -march=pentium2 
RC=windres -O coff

petsend.dll: petsend.o petsend.res
	${CC} -shared petsend.o petsend.res avisynth.lib wsock32.lib -o petsend.dll
	strip petsend.dll

petsend.o: petsend.c
	${CC} -c petsend.c

petsend.res: petsend.rc
	${RC} -i petsend.rc -o petsend.res

