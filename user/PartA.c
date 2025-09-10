#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int
main(int argc, char *argv[]){
	printf(1,"The system has called getpid() %d times.\n",(int)FirstPart());
	exit();
}
