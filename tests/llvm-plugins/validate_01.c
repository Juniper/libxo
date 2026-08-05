#include <libxo/xo.h>

/* tv_syntax_ok: valid format, no diagnostic */
void tv_syntax_ok(void) { xo_emit("{:name/%s}", "alice"); }

/* tv_missing_brace: unclosed field */
void tv_missing_brace(void) { xo_emit("{:bad/", "oops"); }

/* tv_count_too_few: fewer varargs than fields */
void tv_count_too_few(void) { xo_emit("{:name/%s} {:age/%d}", "bob"); }

/* tv_count_too_many: more varargs than fields */
void tv_count_too_many(void) { xo_emit("{:name/%s}", "carol", 99); }

/* tv_type_int_string: %d field gets string arg */
void tv_type_int_string(void) { xo_emit("{:count/%d}", "oops"); }

/* tv_type_string_int: %s field gets int arg */
void tv_type_string_int(void) { xo_emit("{:name/%s}", 42); }

/* tv_multiple_roles: multiple role characters */
void tv_multiple_roles(void) { xo_emit("{LT:Max}"); }

/* tv_name_no_name: empty field name */
void tv_name_no_name(void) { xo_emit("{:/%s}", "x"); }

/* tv_name_underscore: underscore in field name */
void tv_name_underscore(void) { xo_emit("{:no_good/%s}", "x"); }

/* tv_name_uppercase: uppercase in field name */
void tv_name_uppercase(void) { xo_emit("{:NAME/%s}", "x"); }

/* tv_name_digit: name starting with digit */
void tv_name_digit(void) { xo_emit("{:9lives/%s}", "x"); }

/* tv_name_invalid_char: invalid character in field name */
void tv_name_invalid_char(void) { xo_emit("{:a$b/%s}", "x"); }

/* tv_name_too_short: field name too short */
void tv_name_too_short(void) { xo_emit("{:ab/%s}", "x"); }

/* tv_anchor_nonnumeric: non-numeric anchor content */
void tv_anchor_nonnumeric(void) { xo_emit("{[:mumble}{:name/%s}{]:}", "x"); }

/* tv_anchor_bad_format: anchor with non-%d format */
void tv_anchor_bad_format(void) { xo_emit("{[:/%s}{:name/%s}{]:}", "32", "x"); }

/* tv_anchor_both: anchor with both content and format */
void tv_anchor_both(void) { xo_emit("{[:32/%d}{:name/%s}{]:}", 32, "x"); }

/* tv_humanize_no_format: humanize modifier without format */
void tv_humanize_no_format(void) { xo_emit("{h:size}", 1024); }

/* tv_long_name_bad: role name too long */
void tv_long_name_bad(void) { xo_emit("{,humanization:size/%d}", 1024); }
