/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxo.h"

xo_info_t info[] = {
    { "employee", "object", "Employee data" },
    { "first-name", "string", "First name of employee" },
    { "last-name", "string", "Last name of employee" },
    { "department", "number", "Department number" },
    { "percent-time", "number", "Percentage of full & part time (%)" },
};
int info_count = (sizeof(info) / sizeof(info[0]));

int
main (int argc, char **argv)
{
    struct employee {
	const char *e_first;
	const char *e_last;
	unsigned e_dept;
	unsigned e_percent;
    } employees[] = {
	{ "Terry (\"<one\")", "Jones", 660, 90 },
	{ "Leslie (\"Les\")", "Patterson", 341,60 },
	{ "Ashley (\"Ash\")", "Meter & Smith", 1440, 40 },
	{ NULL, NULL }
    }, *ep = employees;

    xo_set_info(NULL, info, info_count);

    xo_open_container("employees");
    xo_open_list("employee");

    xo_emit("{T:First Name/%-20s}{T:Last Name/%-14s}"
	    "{T:/%-12s}{T:Time (%)}\n", "Department");
    for ( ; ep->e_first; ep++) {
	xo_open_instance("employee");
	xo_emit("{:first-name/%-20s/%s}{:last-name/%-14s/%s}"
		"{:department/%8u/%u}{:percent-time/%8u/%u}\n",
		ep->e_first, ep->e_last, ep->e_dept, ep->e_percent);
	if (ep->e_percent > 50) {
	    xo_attr("full-time", "%s", "honest & for true");
	    xo_emit("{d:benefits/%s}", "full");
	}
	xo_close_instance("employee");
    }

    xo_close_list("employee");
    xo_close_container("employees");

    return 0;
}
