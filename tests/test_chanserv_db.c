#include "chanserv_db.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){char p[]="/tmp/scratchircd-chanserv-XXXXXX";int fd=mkstemp(p);ChanServDb db={0};ChanServChannel r;assert(fd>=0);close(fd);unlink(p);assert(chanserv_db_open(&db,p)==0);assert(chanserv_db_create(&db,"#Test","Alice","Example channel")==0);assert(chanserv_db_get(&db,"#test",&r)==1);assert(strcmp(r.founder,"Alice")==0);assert(strcmp(r.description,"Example channel")==0);assert(r.enabled==1);assert(chanserv_db_set_description(&db,"#TEST","Changed")==0);assert(chanserv_db_set_founder(&db,"#test","Bob")==0);assert(chanserv_db_set_enabled(&db,"#test",0)==0);assert(chanserv_db_get(&db,"#test",&r)==1);assert(strcmp(r.founder,"Bob")==0&&strcmp(r.description,"Changed")==0&&r.enabled==0);assert(chanserv_db_delete(&db,"#test")==0);assert(chanserv_db_get(&db,"#test",&r)==0);chanserv_db_close(&db);unlink(p);puts("chanserv db tests passed");return 0;}
