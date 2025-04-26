#!/bin/sh

OPS='
add_one
close_container
close_field
close_instance
create
destroy
get_status
open_container
open_field
open_instance
passthru
status_name
'

comment_add_one="Add a filter (xpath) to our filtering mechanism"
args_add_one="xop, vp"
signature_add_one="xo_handle_t *xop UNUSED, const char *vp UNUSED"

args_open_field="xop, xfp, tag, tlen"
signature_open_field="xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,\
                       const char *tag UNUSED, ssize_t tlen UNUSED"

args_close_field="xop, xfp, tag, tlen"
signature_close_field="xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED,\
                       const char *tag UNUSED, ssize_t tlen UNUSED"

args_create="xop"
signature_create="xo_handle_t *xop UNUSED"
return_type_create="xo_filter_t *"

args_destroy="xop, xfp"
signature_destroy="xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED"
return_type_destroy="void"
return_value_destroy="/*void*/"

return_type_get_status=xo_filter_status_t
args_get_status="xop, xfp"
signature_get_status="xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED"

args_key="xop, xfp, tag, tlen, value, vlen"
signature_key="xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
               const char *tag UNUSED, xo_ssize_t tlen UNUSED, \
               const char *value UNUSED, xo_ssize_t vlen UNUSED"

comment_status_name="Turn a xo_filter_status_t into a string for debug output"
return_type_status_name="const char *"
args_status_name="rc"
signature_status_name="xo_filter_status_t rc UNUSED"
return_value_status_name="\"unknown\""

args_passthru="xop, op, bufp, name, value, private, flags, func, xfp"
signature_passthru="XO_ENCODER_HANDLER_ARGS, xo_encoder_func_t func UNUSED,\
                      struct xo_filter_s *xfp UNUSED"


TAG_FUNCS='
open_container
close_container
open_instance
close_instance
'

DEFINES='
#define XO_FILTER_XXX_ARGS XO_FILTER_DEFAULT_ARGS
#define XO_FILTER_XXX_SIGNATURE XO_FILTER_DEFAULT_SIGNATURE

typedef XO_FILTER_DEFAULT_RETURN_TYPE (*xo_filter_xxx_func_t)(XO_FILTER_XXX_SIGNATURE);
'

STRUCT='    xo_filter_xxx_func_t xfo_filter_xxx_func;'

FUNC='static inline XO_FILTER_DEFAULT_RETURN_TYPE
xo_filter_xxx (XO_FILTER_XXX_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_xxx_func)
        return xo_filter_ops.xfo_filter_xxx_func(XO_FILTER_XXX_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return XO_FILTER_DEFAULT_RETURN_VALUE;
}
'

HEADER='/*
 * Automatically generated; do not edit
 *
 * While this is automatically generated, we want to control how it
 * changes and when those changes are committed, so we _do_ check it
 * into instead of building it at build time.
 *
 * To rebuild, use "make build_ops".
 */

#ifndef XO_FILTER_OPS_H
#define XO_FILTER_OPS_H
'

FOOTER='
#endif /* XO_FILTER_OPS_H */
'

build_defines() {
    for op in $OPS; do
        name=$op
        NAME=`echo $op | tr '[a-z]' '[A-Z]'`
        defines="$DEFINES"

	: echo == \$args_$name ==
        args=`eval echo "\"\\$args_$name\""`
        : echo ==== $args ====
	args="${args:-XO_FILTER_DEFAULT_ARGS}"

	return_type=`eval echo "\"\\$return_type_$name\""`
	return_type="${return_type:-int}"

	return_value=`eval echo "\"\\$return_value_$name\""`
	return_value="${return_value:-0}"

        sig=`eval echo "\"\\$signature_$name\""`
	sig="${sig:-XO_FILTER_DEFAULT_SIGNATURE}"

        defines=`echo "$defines" | \
              sed -e "s:XO_FILTER_DEFAULT_ARGS:$args:" \
                    -e "s:XO_FILTER_DEFAULT_SIGNATURE:$sig:" \
                    -e "s:XO_FILTER_DEFAULT_RETURN_TYPE:$return_type:" \
                    -e "s:XO_FILTER_DEFAULT_RETURN_VALUE:$return_value:"`

        echo "$defines" | sed -e "s:XXX:$NAME:g" -e "s:xxx:$name:g"
    done
}

build_struct() {
    echo 'typedef struct xo_filter_ops_s {'
    echo '    int xfo_version;'

    for op in $OPS; do
        name=$op
        NAME=`echo $op | tr '[a-z]' '[A-Z]'`
        echo "$STRUCT" | sed -e "s:XXX:$NAME:g" -e "s:xxx:$name:g"
    done

    echo '} xo_filter_ops_t;\n'

    echo 'extern xo_filter_ops_t xo_filter_ops;\n'

}

build_func() {
    for op in $OPS; do
        name=$op
        NAME=`echo $op | tr '[a-z]' '[A-Z]'`

        comment=`eval echo "\"\\$comment_$name\""`
        : echo ==== $comment ====
        if [ ! -z "$comment" ]; then
            echo "/*\n * $comment\n */"
        fi

	return_type=`eval echo "\"\\$return_type_$name\""`
	return_type="${return_type:-int}"

	return_value=`eval echo "\"\\$return_value_$name\""`
	return_value="${return_value:-0}"

        echo "$FUNC" | sed -e "s:XXX:$NAME:g" -e "s:xxx:$name:g" \
	    -e "s:XO_FILTER_DEFAULT_RETURN_TYPE:$return_type:g" \
	    -e "s:XO_FILTER_DEFAULT_RETURN_VALUE:$return_value:g"
    done
}

build_func_names() {
    echo "#define XO_FILTER_OPS_FUNCS \\"
    for op in $OPS; do
        name=$op
        NAME=`echo $op | tr '[a-z]' '[A-Z]'`

	echo "    xo_filter_op_$name, \\"
    done

    echo "    /* end */"
}

for op in $TAG_FUNCS; do
    eval args_$op=XO_FILTER_DEFAULT_TAG_ARGS
    eval signature_$op=XO_FILTER_DEFAULT_TAG_SIGNATURE
done


build_header() {
    echo "$HEADER"
}

build_footer() {
    echo "$FOOTER"
}

build_header
build_defines
build_struct
build_func
build_func_names
build_footer

exit 0
