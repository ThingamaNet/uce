
#include "types.h"
#include "hash.h"
#include "functionlib.h"
#include "sys.h"
#include "uri.h"
#include "compiler.h"
#include "mysql-connector.h"

extern "C" void set_current_request(Request* _request)
{
	context = _request;
	signal(SIGSEGV, on_segfault);
}
