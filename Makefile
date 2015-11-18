LIBM = -lm
LIBRT = -lrt
LIBPTHREAD = -lpthread

CFLAGS = -g `xml2-config --cflags --libs`

EXE = sched

all: ${EXE}

sched: sched.o
	${CC} -o $@ sched.c ${CFLAGS} ${LIBPTHREAD} ${LIBM} ${LIBRT}

clean: 
	${RM} ${EXE} *.o
