/* Tools not built yet: each reports a failure. */

#include "mt.h"

static void todo(mt_run_t *r)
{
    r->result = MT_FAIL;
    mt_keys(r, "why=todo");
    mt_text(r, "Not built yet.");
}

void mt_tool_view(mt_run_t *r) { todo(r); }
void mt_tool_clock(mt_run_t *r) { todo(r); }
void mt_tool_users(mt_run_t *r) { todo(r); }
