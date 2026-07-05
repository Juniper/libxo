/*
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


#define XO_FILTER_ADD_ONE_ARGS xop, vp
#define XO_FILTER_ADD_ONE_SIGNATURE \
    xo_handle_t *xop UNUSED, const char *vp UNUSED

typedef int (*xo_filter_add_one_func_t)(XO_FILTER_ADD_ONE_SIGNATURE);

#define XO_FILTER_CLOSE_CONTAINER_ARGS XO_FILTER_DEFAULT_TAG_ARGS
#define XO_FILTER_CLOSE_CONTAINER_SIGNATURE XO_FILTER_DEFAULT_TAG_SIGNATURE

typedef int (*xo_filter_close_container_func_t)(XO_FILTER_CLOSE_CONTAINER_SIGNATURE);

#define XO_FILTER_CLOSE_FIELD_ARGS xop, xfp, tag, tlen
#define XO_FILTER_CLOSE_FIELD_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED, ssize_t tlen UNUSED

typedef int (*xo_filter_close_field_func_t)(XO_FILTER_CLOSE_FIELD_SIGNATURE);

#define XO_FILTER_CLOSE_INSTANCE_ARGS XO_FILTER_DEFAULT_TAG_ARGS
#define XO_FILTER_CLOSE_INSTANCE_SIGNATURE XO_FILTER_DEFAULT_TAG_SIGNATURE

typedef int (*xo_filter_close_instance_func_t)(XO_FILTER_CLOSE_INSTANCE_SIGNATURE);

#define XO_FILTER_CREATE_ARGS xop
#define XO_FILTER_CREATE_SIGNATURE xo_handle_t *xop UNUSED

typedef xo_filter_t * (*xo_filter_create_func_t)(XO_FILTER_CREATE_SIGNATURE);

#define XO_FILTER_DESTROY_ARGS xop, xfp
#define XO_FILTER_DESTROY_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED

typedef void (*xo_filter_destroy_func_t)(XO_FILTER_DESTROY_SIGNATURE);

#define XO_FILTER_GET_STATUS_ARGS xop, xfp
#define XO_FILTER_GET_STATUS_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED

typedef xo_filter_status_t (*xo_filter_get_status_func_t)(XO_FILTER_GET_STATUS_SIGNATURE);

#define XO_FILTER_KEY_ARGS xop, xfp, tag, tlen, value, vlen
#define XO_FILTER_KEY_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED, xo_ssize_t tlen UNUSED, \
	const char *value UNUSED, xo_ssize_t vlen UNUSED

typedef int (*xo_filter_key_func_t)(XO_FILTER_KEY_SIGNATURE);

#define XO_FILTER_NEEDS_NONKEY_FIELD_ARGS xop, xfp, tag, tlen
#define XO_FILTER_NEEDS_NONKEY_FIELD_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED, xo_ssize_t tlen UNUSED

typedef int (*xo_filter_needs_nonkey_field_func_t)(XO_FILTER_NEEDS_NONKEY_FIELD_SIGNATURE);

#define XO_FILTER_OPEN_CONTAINER_ARGS XO_FILTER_DEFAULT_TAG_ARGS
#define XO_FILTER_OPEN_CONTAINER_SIGNATURE XO_FILTER_DEFAULT_TAG_SIGNATURE

typedef int (*xo_filter_open_container_func_t)(XO_FILTER_OPEN_CONTAINER_SIGNATURE);

#define XO_FILTER_OPEN_FIELD_ARGS xop, xfp, tag, tlen
#define XO_FILTER_OPEN_FIELD_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED, ssize_t tlen UNUSED

typedef int (*xo_filter_open_field_func_t)(XO_FILTER_OPEN_FIELD_SIGNATURE);

#define XO_FILTER_OPEN_INSTANCE_ARGS XO_FILTER_DEFAULT_TAG_ARGS
#define XO_FILTER_OPEN_INSTANCE_SIGNATURE XO_FILTER_DEFAULT_TAG_SIGNATURE

typedef int (*xo_filter_open_instance_func_t)(XO_FILTER_OPEN_INSTANCE_SIGNATURE);

#define XO_FILTER_PASSTHRU_ARGS xop, op, bufp, name, value, private, flags, func, xfp
#define XO_FILTER_PASSTHRU_SIGNATURE \
    XO_ENCODER_HANDLER_ARGS, xo_encoder_func_t func UNUSED, \
	struct xo_filter_s *xfp UNUSED

typedef int (*xo_filter_passthru_func_t)(XO_FILTER_PASSTHRU_SIGNATURE);

#define XO_FILTER_PRED_FIELD_ARGS xop, xfp, tag, tlen, value, vlen
#define XO_FILTER_PRED_FIELD_SIGNATURE \
    xo_handle_t *xop UNUSED, xo_filter_t *xfp UNUSED, \
	const char *tag UNUSED, xo_ssize_t tlen UNUSED, \
	const char *value UNUSED, xo_ssize_t vlen UNUSED

typedef int (*xo_filter_pred_field_func_t)(XO_FILTER_PRED_FIELD_SIGNATURE);

#define XO_FILTER_STATUS_NAME_ARGS rc
#define XO_FILTER_STATUS_NAME_SIGNATURE xo_filter_status_t rc UNUSED

typedef const char * (*xo_filter_status_name_func_t)(XO_FILTER_STATUS_NAME_SIGNATURE);
typedef struct xo_filter_ops_s {
    int xfo_version;
    xo_filter_add_one_func_t xfo_filter_add_one_func;
    xo_filter_close_container_func_t xfo_filter_close_container_func;
    xo_filter_close_field_func_t xfo_filter_close_field_func;
    xo_filter_close_instance_func_t xfo_filter_close_instance_func;
    xo_filter_create_func_t xfo_filter_create_func;
    xo_filter_destroy_func_t xfo_filter_destroy_func;
    xo_filter_get_status_func_t xfo_filter_get_status_func;
    xo_filter_key_func_t xfo_filter_key_func;
    xo_filter_needs_nonkey_field_func_t xfo_filter_needs_nonkey_field_func;
    xo_filter_open_container_func_t xfo_filter_open_container_func;
    xo_filter_open_field_func_t xfo_filter_open_field_func;
    xo_filter_open_instance_func_t xfo_filter_open_instance_func;
    xo_filter_passthru_func_t xfo_filter_passthru_func;
    xo_filter_pred_field_func_t xfo_filter_pred_field_func;
    xo_filter_status_name_func_t xfo_filter_status_name_func;
} xo_filter_ops_t;

extern xo_filter_ops_t xo_filter_ops;

/*
 * Add a filter (xpath) to our filtering mechanism
 */
static inline int
xo_filter_add_one (XO_FILTER_ADD_ONE_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_add_one_func)
        return xo_filter_ops.xfo_filter_add_one_func(XO_FILTER_ADD_ONE_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_close_container (XO_FILTER_CLOSE_CONTAINER_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_close_container_func)
        return xo_filter_ops.xfo_filter_close_container_func(XO_FILTER_CLOSE_CONTAINER_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_close_field (XO_FILTER_CLOSE_FIELD_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_close_field_func)
        return xo_filter_ops.xfo_filter_close_field_func(XO_FILTER_CLOSE_FIELD_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_close_instance (XO_FILTER_CLOSE_INSTANCE_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_close_instance_func)
        return xo_filter_ops.xfo_filter_close_instance_func(XO_FILTER_CLOSE_INSTANCE_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline xo_filter_t *
xo_filter_create (XO_FILTER_CREATE_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_create_func)
        return xo_filter_ops.xfo_filter_create_func(XO_FILTER_CREATE_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline void
xo_filter_destroy (XO_FILTER_DESTROY_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_destroy_func)
        return xo_filter_ops.xfo_filter_destroy_func(XO_FILTER_DESTROY_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return /*void*/;
}

static inline xo_filter_status_t
xo_filter_get_status (XO_FILTER_GET_STATUS_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_get_status_func)
        return xo_filter_ops.xfo_filter_get_status_func(XO_FILTER_GET_STATUS_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_key (XO_FILTER_KEY_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_key_func)
        return xo_filter_ops.xfo_filter_key_func(XO_FILTER_KEY_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_needs_nonkey_field (XO_FILTER_NEEDS_NONKEY_FIELD_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_needs_nonkey_field_func)
        return xo_filter_ops.xfo_filter_needs_nonkey_field_func(XO_FILTER_NEEDS_NONKEY_FIELD_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_open_container (XO_FILTER_OPEN_CONTAINER_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_open_container_func)
        return xo_filter_ops.xfo_filter_open_container_func(XO_FILTER_OPEN_CONTAINER_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_open_field (XO_FILTER_OPEN_FIELD_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_open_field_func)
        return xo_filter_ops.xfo_filter_open_field_func(XO_FILTER_OPEN_FIELD_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_open_instance (XO_FILTER_OPEN_INSTANCE_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_open_instance_func)
        return xo_filter_ops.xfo_filter_open_instance_func(XO_FILTER_OPEN_INSTANCE_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_passthru (XO_FILTER_PASSTHRU_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_passthru_func)
        return xo_filter_ops.xfo_filter_passthru_func(XO_FILTER_PASSTHRU_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

static inline int
xo_filter_pred_field (XO_FILTER_PRED_FIELD_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_pred_field_func)
        return xo_filter_ops.xfo_filter_pred_field_func(XO_FILTER_PRED_FIELD_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return 0;
}

/*
 * Turn a xo_filter_status_t into a string for debug output
 */
static inline const char *
xo_filter_status_name (XO_FILTER_STATUS_NAME_SIGNATURE)
{
#ifdef LIBXO_NEED_FILTERS
    if (xo_filter_ops.xfo_filter_status_name_func)
        return xo_filter_ops.xfo_filter_status_name_func(XO_FILTER_STATUS_NAME_ARGS);
#endif /* LIBXO_NEED_FILTERS */
    return "unknown";
}

#define XO_FILTER_OPS_FUNCS \
    xo_filter_op_add_one, \
    xo_filter_op_close_container, \
    xo_filter_op_close_field, \
    xo_filter_op_close_instance, \
    xo_filter_op_create, \
    xo_filter_op_destroy, \
    xo_filter_op_get_status, \
    xo_filter_op_key, \
    xo_filter_op_needs_nonkey_field, \
    xo_filter_op_open_container, \
    xo_filter_op_open_field, \
    xo_filter_op_open_instance, \
    xo_filter_op_passthru, \
    xo_filter_op_pred_field, \
    xo_filter_op_status_name, \
    /* end */

#endif /* XO_FILTER_OPS_H */

