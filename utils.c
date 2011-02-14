#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "common.h"
#include "utils.h"
#include "log.h"

void _Assert (char* name, char* strFile, unsigned uLine)
{
	dump(L_ERROR, "Assertion failed: %s, %s, line %u", name, strFile, uLine);
	abort();
}

